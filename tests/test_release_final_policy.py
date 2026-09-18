from __future__ import annotations

import json
import subprocess
import sys
from copy import deepcopy
from pathlib import Path

import pytest

from scripts import release_identity, run_release_gate, uce_tsan
from scripts.uce_gate import candidate
from tests.test_release_gate_unit import _full_manifest_fixture
from tests.test_release_gate_unit import _init_identity_repo
from tests.test_release_identity import _git, _init_repo

EXPECTED_GATES = {
    "package_gate_contract",
    "shared_package_candidate",
    "shared_public_api",
    "shared_package_evidence",
    "static_configure",
    "static_package_candidate",
    "static_build",
    "static_package_evidence",
    "strict_qttest",
    "examples_benchmarks_configure",
    "examples_benchmarks_build",
    "deprecated_curl_api_guard",
    "label_matrix_guard",
    "skip_contract_guard",
    "full_ctest",
    "clang_ctest",
    "libcurl_consistency_full",
    "dynamic_symbol_allowlist",
    "other_extras_dynamic_symbol_allowlist",
    "package_asan_ubsan_lsan",
    "capability_matrix_build",
    "capability_matrix_probe",
    "metadata_scan",
    "uce_evidence",
    "doxygen_report",
}
EXPECTED_ARTIFACTS = {
    "full_ctest_report",
    "clang_ctest_report",
    "parity_report",
    "shared_install_consumer_report",
    "static_install_consumer_report",
    "shared_lifecycle_report",
    "static_lifecycle_report",
    "asan_ubsan_lsan_report",
    "uce_report",
    "doxygen_report",
    "core_dynamic_symbols",
    "other_extras_dynamic_symbols",
    "capability_matrix_report",
}
EXPECTED_TREES = {
    "release-shared",
    "release-static",
    "test-shared-gcc",
    "test-shared-clang",
    "asan-ubsan-lsan",
}


def _verify(fixture, manifest=None) -> int:
    repo, args, steps, original = fixture
    release_identity.write_snapshot(
        args.manifest, manifest if manifest is not None else original
    )
    return run_release_gate._verify_manifest(args, repo, steps)


def test_final_registry_and_verification_need_only_five_producers(
    tmp_path: Path,
) -> None:
    fixture = _full_manifest_fixture(tmp_path)
    _, args, steps, manifest = fixture

    run_release_gate._validate_build_capabilities(args)
    assert len(steps) == len(EXPECTED_GATES)
    assert {step.name for step in steps} == EXPECTED_GATES
    assert set(manifest["gates"]["results"]) == EXPECTED_GATES
    assert set(manifest["gates"]["required"]) == EXPECTED_GATES
    assert set(manifest["evidence_contract"]["artifact_ids"]) == EXPECTED_ARTIFACTS
    assert set(manifest["identity"]["capabilities"]["tree_registry"]) == EXPECTED_TREES
    assert not any("tsan" in part.lower() for step in steps for part in step.command)
    assert _verify(fixture) == 0


def test_script_entrypoint_verifies_candidate_without_pythonpath(
    tmp_path: Path,
) -> None:
    repo, args, _, _ = _full_manifest_fixture(tmp_path)
    argv = ["--tier", "full", "--stage", "final", "--manifest", str(args.manifest)]
    for tree_id, record in run_release_gate._tree_registry(args).items():
        argv.extend((f"--{tree_id}-build-dir", str(record["path"])))
    command = (
        "import runpy, sys; from pathlib import Path; "
        f"sys.path.insert(0, {str(Path(run_release_gate.__file__).parent)!r}); "
        f"entry = runpy.run_path({run_release_gate.__file__!r}); "
        f"args = entry['build_parser']().parse_args({argv!r}); "
        "raise SystemExit(entry['_verify_manifest'](args, "
        f"Path({str(repo)!r}), entry['_selected_steps'](args)))"
    )
    result = subprocess.run(
        [sys.executable, "-I", "-c", command],
        cwd=tmp_path,
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, result.stderr
    assert json.loads(result.stdout) == {"result": "pass", "valid": True, "reasons": []}


@pytest.mark.parametrize("status", [None, "fail", "NOT_RUN"])
def test_independent_tsan_absence_or_failure_does_not_change_final(
    tmp_path: Path,
    status: str | None,
) -> None:
    fixture = _full_manifest_fixture(tmp_path)
    repo, _, _, _ = fixture
    diagnostic = repo / "build" / "diagnostic-tsan" / "report.json"
    if status is not None:
        diagnostic.parent.mkdir(parents=True)
        diagnostic.write_text(
            json.dumps({"result": status, "returncode": 3}), encoding="utf-8"
        )

    assert _verify(fixture) == 0


def test_release_cli_rejects_removed_tsan_argument() -> None:
    with pytest.raises(SystemExit) as error:
        run_release_gate.build_parser().parse_args(["--tsan-build-dir", "build-tsan"])
    assert error.value.code == 2


@pytest.mark.parametrize("state", ["missing", "fail", "NOT_RUN"])
def test_every_non_tsan_required_gate_still_blocks_final(
    tmp_path: Path, state: str
) -> None:
    fixture = _full_manifest_fixture(tmp_path)
    original = fixture[3]
    for gate in sorted(EXPECTED_GATES):
        manifest = deepcopy(original)
        if state == "missing":
            manifest["gates"]["results"].pop(gate)
        else:
            manifest["gates"]["results"][gate].update(result=state, returncode=1)
        assert _verify(fixture, manifest) == 1, gate


def test_every_required_artifact_still_blocks_final_when_missing(
    tmp_path: Path,
) -> None:
    fixture = _full_manifest_fixture(tmp_path)
    original = fixture[3]
    for artifact in sorted(EXPECTED_ARTIFACTS):
        manifest = deepcopy(original)
        manifest["artifacts"].pop(artifact)
        assert _verify(fixture, manifest) == 1, artifact


@pytest.mark.parametrize(
    "mutation", ["missing", "duplicate", "replace", "extra-result"]
)
def test_final_gate_registry_is_checked_by_id_not_count(
    tmp_path: Path, mutation: str
) -> None:
    fixture = _full_manifest_fixture(tmp_path)
    manifest = deepcopy(fixture[3])
    gate = "strict_qttest"
    required = manifest["gates"]["required"]
    if mutation == "missing":
        required.remove(gate)
        manifest["gates"]["results"].pop(gate)
    elif mutation == "duplicate":
        required.append(gate)
    elif mutation == "replace":
        required[required.index(gate)] = "invented_gate"
        manifest["gates"]["results"]["invented_gate"] = manifest["gates"][
            "results"
        ].pop(gate)
    else:
        manifest["gates"]["results"]["invented_gate"] = manifest["gates"]["results"][
            gate
        ]
    assert _verify(fixture, manifest) == 1


def test_final_rejects_duplicate_artifact_contract_ids(tmp_path: Path) -> None:
    fixture = _full_manifest_fixture(tmp_path)
    manifest = deepcopy(fixture[3])
    manifest["evidence_contract"]["artifact_ids"].append("asan_ubsan_lsan_report")
    assert _verify(fixture, manifest) == 1


def test_old_six_tree_manifest_is_not_current_policy_evidence(tmp_path: Path) -> None:
    fixture = _full_manifest_fixture(tmp_path)
    repo, _, _, original = fixture
    manifest = deepcopy(original)
    manifest["identity"]["capabilities"]["tree_registry"]["tsan"] = {
        "tree_id": "tsan",
        "path": str(repo / "build" / "tsan"),
    }
    command = ["python3", "scripts/run_uce_sanitizers.py", "--profile", "tsan"]
    manifest["identity"]["commands"].append(command)
    manifest["gates"]["required"].append("package_tsan")
    manifest["gates"]["results"]["package_tsan"] = {
        "result": "pass",
        "returncode": 0,
        "command": command,
        "producerTreeId": "tsan",
    }
    manifest["identity_digest"] = release_identity._digest(manifest["identity"])
    assert _verify(fixture, manifest) == 1


@pytest.mark.parametrize("field", ["command", "producerTreeId"])
def test_final_rejects_gate_provenance_drift_without_report(
    tmp_path: Path, field: str
) -> None:
    fixture = _full_manifest_fixture(tmp_path)
    manifest = deepcopy(fixture[3])
    manifest["gates"]["results"]["strict_qttest"][field] = (
        ["true"] if field == "command" else "release-shared"
    )
    assert _verify(fixture, manifest) == 1


@pytest.mark.parametrize("stage", ["configure", "build", "subject"])
def test_sanitizer_nonzero_status_cannot_be_relabelled_pass(
    tmp_path: Path, stage: str
) -> None:
    fixture = _full_manifest_fixture(tmp_path)
    _mutate_sanitizer_report(
        fixture, lambda report: report.update({f"{stage}_returncode": 23})
    )
    assert _verify(fixture) == 1


def _mutate_sanitizer_report(fixture, mutate) -> None:
    repo, _, _, manifest = fixture
    artifact = manifest["artifacts"]["asan_ubsan_lsan_report"]
    path = repo / artifact["path"]
    report = json.loads(path.read_text(encoding="utf-8"))
    mutate(report)
    path.write_text(json.dumps(report), encoding="utf-8")
    artifact["sha256"] = release_identity.file_sha256(path)


def _rebind_manifest_identity(fixture) -> None:
    repo, args, steps, manifest = fixture
    manifest["identity"] = release_identity.build_identity(
        repo,
        build_dirs=run_release_gate._identity_build_dirs(args),
        authority_paths=[],
        commands=[step.command for step in steps],
        tree_registry=run_release_gate._tree_registry(args),
    )
    manifest["identity_digest"] = release_identity._digest(manifest["identity"])


@pytest.mark.parametrize("changed", ["tracked", "untracked", "authority", "submodule"])
def test_old_stable_sanitizer_report_is_rejected_for_new_candidate(
    tmp_path: Path,
    changed: str,
    capsys,
) -> None:
    repo = _init_identity_repo(tmp_path)
    if changed == "submodule":
        child = _init_repo(tmp_path / "child")
        _git(repo, "submodule", "add", "-q", str(child), "dependency")
        _git(repo, "commit", "-qam", "add dependency")
    fixture = _full_manifest_fixture(tmp_path, repo=repo)
    manifest = fixture[3]
    artifact = manifest["artifacts"]["asan_ubsan_lsan_report"]
    path = repo / artifact["path"]
    original_bytes = path.read_bytes()
    report = json.loads(original_bytes)
    original_candidate = report["candidate_before"]["combined"]
    assert original_candidate == report["candidate_after"]["combined"]
    changed_path = {
        "tracked": repo / "tracked.txt",
        "untracked": repo / "new-source.txt",
        "authority": repo / "docs/dev/release/2.0.0-hard-break-release-contract.md",
        "submodule": repo / "dependency/subject.txt",
    }[changed]
    changed_path.write_text("new candidate content\n", encoding="utf-8")
    current = candidate.capture_candidate_fingerprint(
        repo, excluded_paths=(path.parent,)
    )
    assert current["combined"] != original_candidate
    _rebind_manifest_identity(fixture)
    artifact["sha256"] = release_identity.file_sha256(path)
    assert _verify(fixture) == 1
    assert json.loads(capsys.readouterr().out)["reasons"] == [
        "sanitizer report candidate does not match current candidate"
    ]
    assert path.read_bytes() == original_bytes


@pytest.mark.parametrize(
    "failure",
    [
        OSError("unreadable"),
        RuntimeError("Git failed"),
        ValueError("invalid path"),
        subprocess.TimeoutExpired("git", 30),
    ],
)
def test_sanitizer_candidate_capture_failure_is_fail_closed(
    tmp_path: Path,
    monkeypatch,
    capsys,
    failure: Exception,
) -> None:
    fixture = _full_manifest_fixture(tmp_path)
    artifact = fixture[3]["artifacts"]["asan_ubsan_lsan_report"]
    report_path = fixture[0] / artifact["path"]
    calls = []

    def fail_capture(repo, *, excluded_paths):
        calls.append((repo, excluded_paths))
        raise failure

    monkeypatch.setattr(candidate, "capture_candidate_fingerprint", fail_capture)
    assert _verify(fixture) == 1
    assert calls == [(fixture[0], (report_path.parent,))]
    assert json.loads(capsys.readouterr().out)["reasons"] == [
        f"sanitizer current candidate identity unavailable: {failure}"
    ]


@pytest.mark.parametrize(
    ("option", "key"),
    [
        ("ASAN_OPTIONS", "detect_leaks"),
        ("ASAN_OPTIONS", "leak_check_at_exit"),
        ("ASAN_OPTIONS", "halt_on_error"),
        ("ASAN_OPTIONS", "exitcode"),
        ("UBSAN_OPTIONS", "halt_on_error"),
        ("UBSAN_OPTIONS", "exitcode"),
        ("LSAN_OPTIONS", "detect_leaks"),
        ("LSAN_OPTIONS", "leak_check_at_exit"),
        ("LSAN_OPTIONS", "exitcode"),
    ],
)
def test_sanitizer_runtime_cannot_disable_detection_or_failure(
    tmp_path: Path,
    option: str,
    key: str,
) -> None:
    fixture = _full_manifest_fixture(tmp_path)

    def disable(report):
        values = dict(
            item.split("=", 1)
            for item in report["sanitizer_options"][option].split(":")
        )
        values[key] = "0"
        report["sanitizer_options"][option] = ":".join(
            f"{name}={value}" for name, value in values.items()
        )

    _mutate_sanitizer_report(fixture, disable)
    assert _verify(fixture) == 1


@pytest.mark.parametrize(
    "mutation", ["empty", "failed-record", "timeout", "changed-candidate", "violation"]
)
def test_sanitizer_requires_real_successful_subject_evidence(
    tmp_path: Path, mutation: str
) -> None:
    fixture = _full_manifest_fixture(tmp_path)

    def invalidate(report):
        if mutation == "empty":
            report["subject_commands"] = []
        elif mutation == "failed-record":
            report["command_results"][0]["returncode"] = 23
        elif mutation == "timeout":
            report["command_results"][0]["timed_out"] = True
        elif mutation == "changed-candidate":
            report["candidate_after"]["combined"] = "sha256:other"
        else:
            report["policy_violations"] = ["sanitizer_subject_failed"]

    _mutate_sanitizer_report(fixture, invalidate)
    assert _verify(fixture) == 1


def test_independent_tsan_still_fails_without_instrumented_qt(tmp_path: Path) -> None:
    (tmp_path / "CMakeCache.txt").write_text(
        "QT_FEATURE_sanitize_thread:INTERNAL=OFF\n", encoding="utf-8"
    )

    def unexpected_run(*args, **kwargs):
        pytest.fail("TSan subjects must not run after failed environment validation")

    result = uce_tsan.run_tsan_subjects(
        tmp_path,
        tmp_path,
        tmp_path,
        {},
        websocket_available=False,
        run=unexpected_run,
    )
    assert result["returncode"] != 0
    assert result["stage"] == "environment"
    assert result["commands"] == []


def test_package_registry_has_no_indirect_tsan_requirement() -> None:
    repo = Path(__file__).resolve().parents[1]
    contract = json.loads(
        (repo / "tests/public_api/package_gate_manifest.json").read_text(
            encoding="utf-8"
        )
    )
    assert set(contract["deliveryTargets"]) == {
        "Core",
        "BlockingExtras",
        "TestSupport",
        "OtherExtras",
    }
    for delivery in contract["deliveryTargets"].values():
        assert delivery["sanitizerEvidence"] == ["asan-ubsan-lsan"]
