from __future__ import annotations

from pathlib import Path
from types import SimpleNamespace
import urllib.error

from tests.qcurl import run_qttest_with_preflight as preflight


def test_main_fails_when_test_binary_is_missing(tmp_path, capsys) -> None:
    rc = preflight.main(
        [
            "--test-bin",
            str(tmp_path / "missing"),
            "--test-name",
            "tst_missing",
        ]
    )

    assert rc == 2
    assert "test binary not found" in capsys.readouterr().err


def test_check_httpbin_requires_env(monkeypatch) -> None:
    monkeypatch.delenv("QCURL_HTTPBIN_URL", raising=False)

    reason = preflight._check_httpbin()

    assert reason is not None
    assert "QCURL_HTTPBIN_URL not set" in reason


def test_check_httpbin_rejects_invalid_url(monkeypatch) -> None:
    monkeypatch.setenv("QCURL_HTTPBIN_URL", "not-a-url")

    assert preflight._check_httpbin() == "invalid QCURL_HTTPBIN_URL: not-a-url"


def test_check_httpbin_reports_probe_failure(monkeypatch) -> None:
    monkeypatch.setenv("QCURL_HTTPBIN_URL", "http://127.0.0.1:1")

    def fake_urlopen(request, timeout):  # type: ignore[no-untyped-def]
        raise urllib.error.URLError("refused")

    monkeypatch.setattr(preflight.urllib.request, "urlopen", fake_urlopen)

    reason = preflight._check_httpbin()

    assert reason is not None
    assert "httpbin health check failed" in reason


def test_check_local_port_reports_bind_failure(monkeypatch) -> None:
    class FakeSocket:
        def bind(self, address):  # type: ignore[no-untyped-def]
            raise OSError("denied")

        def close(self) -> None:
            pass

    monkeypatch.setattr(preflight.socket, "socket", lambda *args, **kwargs: FakeSocket())

    reason = preflight._check_local_port()

    assert reason == "cannot bind 127.0.0.1:0: denied"


def test_check_http2_suite_requires_probe_binary() -> None:
    assert preflight._check_http2_suite(None) == (
        "--http2-probe-bin is required when --require-http2-suite is set"
    )


def test_check_http2_probe_reports_nonzero_exit(tmp_path, monkeypatch) -> None:
    probe = tmp_path / "probe"
    probe.write_text("#!/bin/sh\nexit 1\n", encoding="utf-8")

    def fake_run(cmd, **kwargs):  # type: ignore[no-untyped-def]
        return SimpleNamespace(returncode=1, stderr="no h2\n", stdout="")

    monkeypatch.setattr(preflight.subprocess, "run", fake_run)

    reason = preflight._check_http2_probe(str(probe))

    assert reason == "HTTP/2 capability probe failed: no h2"


def test_fragment_server_assets_require_locked_ws_dependency(tmp_path, monkeypatch) -> None:
    monkeypatch.setattr(preflight, "_repo_root", lambda: tmp_path)
    (tmp_path / "tests" / "qcurl").mkdir(parents=True)
    (tmp_path / "tests" / "qcurl" / "package-lock.json").write_text("{}", encoding="utf-8")

    reason = preflight._check_fragment_server_assets()

    assert reason is not None
    assert "node_modules/ws/package.json" in reason
