from __future__ import annotations

import json
from pathlib import Path
import subprocess
import shutil

import pytest

from scripts.uce_gate import ctest_gates
from scripts.uce_gate import httpbin
from scripts.uce_gate import runtime


_REPO_ROOT = Path(__file__).resolve().parents[1]
_RUNNERS = {
    "offline": ctest_gates.run_offline_ctest_gate,
    "capability": ctest_gates.run_capability_gate,
    "public-api-slow": ctest_gates.run_public_api_slow_gate,
}


def _manifest() -> dict[str, object]:
    return {"policy_violations": [], "results": [], "artifacts": {}, "contracts": {}}


def _stub_capture(command: list[str], **_: object) -> subprocess.CompletedProcess[str]:
    if command[0].endswith("start_httpbin.sh"):
        env_file = Path(command[command.index("--write-env") + 1])
        env_file.parent.mkdir(parents=True, exist_ok=True)
        env_file.write_text(
            'export QCURL_HTTPBIN_URL="http://127.0.0.1:1"\n', encoding="utf-8"
        )
    return subprocess.CompletedProcess(
        command,
        1 if command[0] == "ctest" and "--show-only=json-v1" in command else 0,
        stdout="fixture result\n",
    )


def test_offline_listing_failure_cannot_pass(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    manifest = _manifest()
    monkeypatch.setattr(runtime, "run_capture", _stub_capture)

    ctest_gates.run_offline_ctest_gate(Path.cwd(), tmp_path, tmp_path / "evidence", manifest)

    assert "gate_offline_failed" in manifest["policy_violations"]
    assert manifest["contracts"]["qtest_offline@v1"]["result"] == "fail"


def test_env_listing_failure_cannot_pass(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    manifest = _manifest()
    monkeypatch.setattr(runtime, "run_capture", _stub_capture)

    _, results, violations = httpbin.run_httpbin_gate(
        Path.cwd(), tmp_path, tmp_path / "evidence", manifest
    )

    assert "gate_env_failed" in violations
    assert manifest["contracts"]["qtest_env@v1"]["result"] == "fail"
    assert results[-1].gate_id == "httpbin_stop"


def _ctest_fixture(tmp_path: Path, label: str, outcome: str) -> Path:
    source_dir = tmp_path / "source"
    build_dir = tmp_path / "build"
    source_dir.mkdir()
    lines = [
        "cmake_minimum_required(VERSION 3.20)",
        "project(UceAcceptanceFixture NONE)",
        "enable_testing()",
        'add_test(NAME unselected_failure COMMAND "${CMAKE_COMMAND}" -E false)',
        'set_tests_properties(unselected_failure PROPERTIES LABELS "unselected")',
    ]
    if outcome != "no_tests":
        lines.extend(
            [
                'add_test(NAME selected_pass COMMAND "${CMAKE_COMMAND}" -E echo "Skipped Disabled Sanitizer: diagnostic text")',
                f'set_tests_properties(selected_pass PROPERTIES LABELS "{label}")',
            ]
        )
        command = '"${CMAKE_COMMAND}" -E false'
        if outcome not in {"command_failure", "ctest_skip"}:
            skips = 1 if outcome == "skip" else 0
            command = (
                '"${CMAKE_COMMAND}" -E echo '
                f'"Totals: 1 passed, 0 failed, {skips} skipped, 0 blacklisted, 1ms"'
            )
        lines.extend(
            [
                f"add_test(NAME acceptance_probe COMMAND {command})",
                f'set_tests_properties(acceptance_probe PROPERTIES LABELS "{label}")',
            ]
        )
        if outcome == "disabled":
            lines.append("set_tests_properties(acceptance_probe PROPERTIES DISABLED TRUE)")
        elif outcome == "ctest_skip":
            lines.append("set_tests_properties(acceptance_probe PROPERTIES SKIP_RETURN_CODE 1)")
    (source_dir / "CMakeLists.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    configured = subprocess.run(
        ["cmake", "-S", str(source_dir), "-B", str(build_dir)],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
        timeout=30,
    )
    assert configured.returncode == 0, configured.stdout
    return build_dir


def _stub_httpbin_processes(monkeypatch: pytest.MonkeyPatch) -> None:
    real_capture = runtime.run_capture

    def capture(command: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
        if command[0].endswith(("start_httpbin.sh", "stop_httpbin.sh")):
            return _stub_capture(command, **kwargs)
        return real_capture(command, **kwargs)

    monkeypatch.setattr(runtime, "run_capture", capture)


def _run_label(
    label: str,
    build_dir: Path,
    manifest: dict[str, object],
    monkeypatch: pytest.MonkeyPatch,
) -> list[runtime.GateResult]:
    if label != "env":
        return _RUNNERS[label](_REPO_ROOT, build_dir, build_dir / "evidence", manifest)
    _stub_httpbin_processes(monkeypatch)
    _, results, violations = httpbin.run_httpbin_gate(
        _REPO_ROOT, build_dir, build_dir / "evidence", manifest
    )
    assert ("gate_env_failed" in violations) == (
        manifest["contracts"]["qtest_env@v1"]["result"] == "fail"
    )
    for code in violations:
        if code not in manifest["policy_violations"]:
            manifest["policy_violations"].append(code)
    return [result for result in results if result.gate_id.startswith("ctest_")]


def test_public_api_slow_archives_successful_consumer_output(tmp_path: Path) -> None:
    """Successful build/run output must survive in the archived UCE gate log."""

    build_dir = _ctest_fixture(tmp_path, "public-api-slow", "pass")
    manifest = _manifest()
    results = ctest_gates.run_public_api_slow_gate(
        _REPO_ROOT, build_dir, build_dir / "evidence", manifest
    )
    assert manifest["contracts"]["qtest_public_api_slow@v1"]["result"] == "pass"
    assert results[1].returncode == 0
    output = results[1].log_path.read_text(encoding="utf-8")
    assert "Skipped Disabled Sanitizer: diagnostic text" in output
    assert "Totals: 1 passed, 0 failed, 0 skipped, 0 blacklisted, 1ms" in output


@pytest.mark.parametrize("label", ["offline", "env", "capability"])
def test_real_ctest_qttest_skip_is_rejected(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, label: str
) -> None:
    build_dir = _ctest_fixture(tmp_path, label, "skip")
    raw = subprocess.run(
        ["ctest", "--test-dir", str(build_dir), "-L", f"^{label}$"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
        timeout=30,
    )
    assert raw.returncode == 0, raw.stdout
    monkeypatch.setenv("QCURL_CTEST_MAX_SKIPS", "99")
    monkeypatch.setenv("CTEST", shutil.which("ctest") or "ctest")
    manifest = _manifest()

    results = _run_label(label, build_dir, manifest, monkeypatch)

    assert results[0].returncode == 0
    assert results[1].returncode == 3
    assert manifest["policy_violations"] == [f"gate_{label}_failed"]
    assert manifest["contracts"][f"qtest_{label}@v1"]["result"] == "fail"
    assert "skipped_total=1 > max_skips=0" in results[1].log_path.read_text(encoding="utf-8")


@pytest.mark.parametrize("label", ["offline", "env", "capability", "public-api-slow"])
@pytest.mark.parametrize("outcome", ["pass", "no_tests", "command_failure"])
def test_real_ctest_acceptance_result_is_fail_closed(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, label: str, outcome: str
) -> None:
    build_dir = _ctest_fixture(tmp_path, label, outcome)
    monkeypatch.setenv("CTEST", shutil.which("ctest") or "ctest")
    manifest = _manifest()

    results = _run_label(label, build_dir, manifest, monkeypatch)

    contract_name = label.replace("-", "_")
    passed = outcome == "pass"
    assert (results[1].returncode == 0) is passed
    assert manifest["policy_violations"] == ([] if passed else [f"gate_{contract_name}_failed"])
    contract = manifest["contracts"][f"qtest_{contract_name}@v1"]
    assert contract["result"] == ("pass" if passed else "fail")
    if outcome == "no_tests":
        listing = json.loads(results[0].log_path.read_text(encoding="utf-8"))
        assert listing["tests"] == []
    for artifact in manifest["artifacts"].values():
        assert artifact["required"] is True
        assert (build_dir / "evidence" / artifact["path"]).is_file()


@pytest.mark.parametrize("label", ["offline", "env", "capability", "public-api-slow"])
@pytest.mark.parametrize("outcome", ["disabled", "ctest_skip"])
def test_real_ctest_nonexecution_fails_without_changing_process_exit_code(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, label: str, outcome: str
) -> None:
    build_dir = _ctest_fixture(tmp_path, label, outcome)
    monkeypatch.setenv("CTEST", shutil.which("ctest") or "ctest")
    manifest = _manifest()

    results = _run_label(label, build_dir, manifest, monkeypatch)

    assert [result.returncode for result in results] == [0, 0]
    contract_name = label.replace("-", "_")
    assert manifest["policy_violations"] == [f"gate_{contract_name}_failed"]
    contract = manifest["contracts"][f"qtest_{contract_name}@v1"]
    assert contract["result"] == "fail"
    assert "acceptance_probe" in "\n".join(contract["notes"])
    gate = next(item for item in manifest["results"] if item["id"] == results[1].gate_id)
    assert gate["returncode"] == 0
    assert gate["result"] == "fail"


@pytest.mark.parametrize("corruption", ["missing-result", "duplicate-result", "invalid-listing"])
def test_ctest_incomplete_evidence_fails_even_when_processes_succeed(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, corruption: str
) -> None:
    build_dir = _ctest_fixture(tmp_path, "offline", "pass")
    monkeypatch.setenv("CTEST", shutil.which("ctest") or "ctest")
    capture = runtime.run_capture

    def corrupt_output(command: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
        result = capture(command, **kwargs)
        if "--show-only=json-v1" in command:
            if corruption == "invalid-listing":
                result.stdout = "incomplete CTest listing\n"
        elif corruption != "invalid-listing":
            lines = result.stdout.splitlines(keepends=True)
            target = next(line for line in lines if " Test #" in line and "acceptance_probe" in line)
            lines.remove(target)
            if corruption == "duplicate-result":
                lines.extend([target, target])
            result.stdout = "".join(lines)
        return result

    monkeypatch.setattr(runtime, "run_capture", corrupt_output)
    manifest = _manifest()

    results = ctest_gates.run_offline_ctest_gate(
        _REPO_ROOT, build_dir, build_dir / "evidence", manifest
    )

    assert [result.returncode for result in results] == [0, 0]
    assert manifest["policy_violations"] == ["gate_offline_failed"]
    contract = manifest["contracts"]["qtest_offline@v1"]
    assert contract["result"] == "fail"
    if corruption != "invalid-listing":
        assert "acceptance_probe" in "\n".join(contract["notes"])
