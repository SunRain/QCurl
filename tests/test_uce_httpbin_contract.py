from __future__ import annotations

from pathlib import Path
import subprocess

import pytest

from scripts.uce_gate import httpbin
from scripts.uce_gate import runtime


def _manifest() -> dict[str, object]:
    return {"policy_violations": [], "results": [], "artifacts": {}, "contracts": {}}


def _install_httpbin_commands(
    monkeypatch: pytest.MonkeyPatch,
    payload: bytes | None,
    *,
    start_returncode: int = 0,
    stop_returncode: int = 0,
) -> list[list[str]]:
    commands: list[list[str]] = []

    def capture(command: list[str], **_: object) -> subprocess.CompletedProcess[str]:
        commands.append(command)
        returncode = 0
        output = "fixture result\n"
        if command[0].endswith("start_httpbin.sh"):
            returncode = start_returncode
            if payload is not None:
                path = Path(command[command.index("--write-env") + 1])
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(payload)
        elif command[0].endswith("stop_httpbin.sh"):
            returncode = stop_returncode
        elif "--show-only=json-v1" in command:
            output = '{"tests": [{"name": "env_probe"}]}\n'
        else:
            output = "1/1 Test #1: env_probe ..................... Passed 0.01 sec\n"
        return subprocess.CompletedProcess(command, returncode, stdout=output)

    monkeypatch.setattr(runtime, "run_capture", capture)
    return commands


@pytest.mark.parametrize(
    "payload",
    [
        b'export QCURL_HTTPBIN_URL="http://127.0.0.1:1\n',
        b"export 1QCURL_HTTPBIN_URL=http://127.0.0.1:1\n",
        b"export QCURL_HTTPBIN_URL\n",
        b"QCURL_HTTPBIN_URL=http://127.0.0.1:1\n",
        b"export QCURL_HTTPBIN_URL=http://127.0.0.1:1;false\n",
        b"export QCURL_HTTPBIN_URL=http://127.0.0.1:1 extra\n",
        b'export QCURL_HTTPBIN_URL="http://127.0.0.1:1/\xff"\n',
    ],
)
def test_httpbin_malformed_env_is_rejected_without_running_ctest(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, payload: bytes
) -> None:
    commands = _install_httpbin_commands(monkeypatch, payload)
    manifest = _manifest()

    env_values, _, violations = httpbin.run_httpbin_gate(
        Path.cwd(), tmp_path, tmp_path / "evidence", manifest
    )

    assert env_values == {}
    assert "env_preflight_httpbin_env_parse_error" in violations
    assert "env_preflight_httpbin_url_missing" in violations
    assert manifest["contracts"]["qtest_env@v1"]["result"] == "fail"
    assert len(commands) == 2
    assert commands[-1][0].endswith("stop_httpbin.sh")
    assert (tmp_path / "evidence/httpbin/httpbin_env_parse_error.txt").is_file()


def test_httpbin_start_failure_is_independent_of_env_failures(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    commands = _install_httpbin_commands(
        monkeypatch,
        b'export QCURL_HTTPBIN_URL="http://127.0.0.1:1"\n',
        start_returncode=1,
    )

    _, _, violations = httpbin.run_httpbin_gate(
        Path.cwd(), tmp_path, tmp_path / "evidence", _manifest()
    )

    assert violations == ["env_preflight_httpbin_start_failed"]
    assert commands[-1][0].endswith("stop_httpbin.sh")


def test_httpbin_successful_start_without_env_file_is_rejected(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    commands = _install_httpbin_commands(monkeypatch, None)
    manifest = _manifest()

    env_values, _, violations = httpbin.run_httpbin_gate(
        Path.cwd(), tmp_path, tmp_path / "evidence", manifest
    )

    assert env_values == {}
    assert violations == ["env_preflight_httpbin_env_missing", "env_preflight_httpbin_url_missing"]
    assert manifest["contracts"]["qtest_env@v1"]["result"] == "fail"
    assert len(commands) == 2


@pytest.mark.parametrize(
    "payload",
    [b'export QCURL_HTTPBIN_CONTAINER_NAME="fixture"\n', b'export QCURL_HTTPBIN_URL=""\n'],
)
def test_httpbin_valid_env_without_url_is_rejected(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, payload: bytes
) -> None:
    commands = _install_httpbin_commands(monkeypatch, payload)
    manifest = _manifest()

    _, _, violations = httpbin.run_httpbin_gate(
        Path.cwd(), tmp_path, tmp_path / "evidence", manifest
    )

    assert violations == ["env_preflight_httpbin_url_missing"]
    assert manifest["contracts"]["qtest_env@v1"]["result"] == "fail"
    assert len(commands) == 2
    assert (tmp_path / "evidence/httpbin/httpbin_unavailable.txt").is_file()


def test_httpbin_stop_failure_is_independently_rejected(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    commands = _install_httpbin_commands(
        monkeypatch,
        b'export QCURL_HTTPBIN_URL="http://127.0.0.1:1"\n',
        stop_returncode=1,
    )
    manifest = _manifest()

    _, results, violations = httpbin.run_httpbin_gate(
        Path.cwd(), tmp_path, tmp_path / "evidence", manifest
    )

    assert violations == ["env_preflight_httpbin_stop_failed"]
    assert manifest["contracts"]["qtest_env@v1"]["result"] == "pass"
    assert results[-1].returncode == 1
    assert commands[-1][0].endswith("stop_httpbin.sh")


def test_httpbin_valid_quotes_comments_and_container_identity_are_preserved(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    commands = _install_httpbin_commands(
        monkeypatch,
        b"# generated environment\n\n"
        b'export QCURL_HTTPBIN_URL="http://127.0.0.1:1" # local fixture\n'
        b"export QCURL_HTTPBIN_CONTAINER_NAME='acceptance-fixture'\n",
    )
    manifest = _manifest()

    env_values, _, violations = httpbin.run_httpbin_gate(
        Path.cwd(), tmp_path, tmp_path / "evidence", manifest
    )

    assert violations == []
    assert env_values == {
        "QCURL_HTTPBIN_URL": "http://127.0.0.1:1",
        "QCURL_HTTPBIN_CONTAINER_NAME": "acceptance-fixture",
    }
    assert commands[-1][-2:] == ["--name", "acceptance-fixture"]
    assert manifest["contracts"]["qtest_env@v1"]["result"] == "pass"


def test_httpbin_stops_owned_fixture_when_env_command_raises(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    commands = _install_httpbin_commands(
        monkeypatch,
        b'export QCURL_HTTPBIN_URL="http://127.0.0.1:1"\n'
        b'export QCURL_HTTPBIN_CONTAINER_NAME="owned-fixture"\n',
    )
    capture = runtime.run_capture

    def raise_ctest(command: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
        if command[0] == "ctest":
            raise RuntimeError("synthetic CTest launch failure")
        return capture(command, **kwargs)

    monkeypatch.setattr(runtime, "run_capture", raise_ctest)

    with pytest.raises(RuntimeError, match="synthetic CTest launch failure"):
        httpbin.run_httpbin_gate(Path.cwd(), tmp_path, tmp_path / "evidence", _manifest())

    assert commands[-1][0].endswith("stop_httpbin.sh")
    assert commands[-1][-2:] == ["--name", "owned-fixture"]
