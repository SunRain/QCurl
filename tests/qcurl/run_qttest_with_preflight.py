#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QtTest preflight wrapper for qcurl evidence gates.

- 将“环境未准备好”从测试体内的 QSKIP 前移到 gate 入口
- 缺少前置时直接 fail-closed，避免 ctest 把未取证误读为已覆盖
"""

from __future__ import annotations

import argparse
import os
import shutil
import socket
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _fail(reason: str) -> int:
    sys.stderr.write(f"[qcurl-preflight] failed: {reason}\n")
    return 2


def _check_local_port() -> str | None:
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    except OSError as exc:
        return f"cannot create AF_INET socket: {exc}"
    try:
        sock.bind(("127.0.0.1", 0))
    except OSError as exc:
        return f"cannot bind 127.0.0.1:0: {exc}"
    finally:
        sock.close()
    return None


def _check_python() -> str | None:
    if shutil.which("python3") or shutil.which("python"):
        return None
    return "python3/python not found in PATH"


def _check_node() -> str | None:
    if shutil.which("node"):
        return None
    return "node not found in PATH"


def _check_httpbin() -> str | None:
    url = (os.environ.get("QCURL_HTTPBIN_URL") or "").strip()
    if not url:
        return (
            "QCURL_HTTPBIN_URL not set; start tests/qcurl/httpbin/start_httpbin.sh "
            "and source the generated env file first"
        )

    parsed = urllib.parse.urlparse(url)
    if not parsed.scheme or not parsed.netloc:
        return f"invalid QCURL_HTTPBIN_URL: {url}"

    probe_path = parsed.path.rstrip("/") + "/get"
    probe_url = parsed._replace(path=probe_path, query="", fragment="").geturl()
    request = urllib.request.Request(probe_url, method="GET")
    try:
        with urllib.request.urlopen(request, timeout=3) as response:
            if int(response.status) != 200:
                return f"httpbin health check returned HTTP {response.status}: {probe_url}"
    except urllib.error.URLError as exc:
        return f"httpbin health check failed: {exc}"
    return None


def _validate_url(env_name: str) -> str | None:
    value = (os.environ.get(env_name) or "").strip()
    if not value:
        return None

    parsed = urllib.parse.urlparse(value)
    if not parsed.scheme or not parsed.netloc:
        return f"invalid {env_name}: {value}"
    return None


def _check_http2_probe(probe_bin: str | None) -> str | None:
    if not probe_bin:
        return "--http2-probe-bin is required when --require-http2-suite is set"

    probe_path = Path(probe_bin).resolve()
    if not probe_path.exists():
        return f"HTTP/2 capability probe not found: {probe_path}"

    proc = subprocess.run(
        [str(probe_path)],
        check=False,
        capture_output=True,
        text=True,
    )
    if proc.returncode == 0:
        return None

    detail = (proc.stderr or proc.stdout).strip()
    if detail:
        detail = " ".join(detail.splitlines())
        return f"HTTP/2 capability probe failed: {detail}"
    return f"HTTP/2 capability probe exited with status {proc.returncode}"


def _check_http2_local_server_assets() -> str | None:
    repo_root = _repo_root()
    required_paths = [
        repo_root / "tests" / "qcurl" / "http2-test-server.js",
        repo_root / "tests" / "qcurl" / "testdata" / "http2" / "localhost.crt",
        repo_root / "tests" / "qcurl" / "testdata" / "http2" / "localhost.key",
    ]
    for required_path in required_paths:
        if not required_path.exists():
            return f"missing HTTP/2 local fixture asset: {required_path}"
    return None


def _check_http2_suite(probe_bin: str | None) -> str | None:
    capability_reason = _check_http2_probe(probe_bin)
    if capability_reason:
        return capability_reason

    for env_name in ("QCURL_HTTP2_TEST_BASE_URL", "QCURL_HTTP2_TEST_HTTP1_BASE_URL"):
        reason = _validate_url(env_name)
        if reason:
            return reason

    override_h2 = (os.environ.get("QCURL_HTTP2_TEST_BASE_URL") or "").strip()
    override_http1 = (os.environ.get("QCURL_HTTP2_TEST_HTTP1_BASE_URL") or "").strip()
    if override_h2 and override_http1:
        return None

    node_reason = _check_node()
    if node_reason:
        return f"HTTP/2 local fixture requires node: {node_reason}"

    local_port_reason = _check_local_port()
    if local_port_reason:
        return local_port_reason

    return _check_http2_local_server_assets()


def _check_fragment_server_assets() -> str | None:
    repo_root = _repo_root()
    package_lock = repo_root / "tests" / "qcurl" / "package-lock.json"
    ws_package = repo_root / "tests" / "qcurl" / "node_modules" / "ws" / "package.json"
    if not package_lock.exists():
        return f"missing locked dependency manifest: {package_lock}"
    if not ws_package.exists():
        return (
            "missing tests/qcurl/node_modules/ws/package.json; "
            "fragment echo server cannot be audited or started"
        )
    return None


def _parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run a QtTest binary with gate preflight checks.")
    parser.add_argument("--test-bin", required=True, help="Absolute or relative path to the QtTest binary.")
    parser.add_argument("--test-name", required=True, help="CTest-visible test name.")
    parser.add_argument("--require-httpbin", action="store_true")
    parser.add_argument("--require-http2-suite", action="store_true")
    parser.add_argument("--require-local-port", action="store_true")
    parser.add_argument("--require-python", action="store_true")
    parser.add_argument("--require-node", action="store_true")
    parser.add_argument("--require-fragment-ws", action="store_true")
    parser.add_argument("--http2-probe-bin", help="Path to the HTTP/2 capability probe binary.")
    parser.add_argument("test_args", nargs=argparse.REMAINDER)
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = _parse_args(argv)
    test_bin = Path(args.test_bin).resolve()
    if not test_bin.exists():
        return _fail(f"test binary not found: {test_bin}")

    checks: list[tuple[bool, callable[[], str | None]]] = [
        (args.require_httpbin, _check_httpbin),
        (args.require_local_port, _check_local_port),
        (args.require_python, _check_python),
        (args.require_node, _check_node),
        (args.require_fragment_ws, _check_fragment_server_assets),
    ]
    for enabled, check in checks:
        if not enabled:
            continue
        reason = check()
        if reason:
            return _fail(f"{args.test_name}: {reason}")

    if args.require_http2_suite:
        reason = _check_http2_suite(args.http2_probe_bin)
        if reason:
            return _fail(f"{args.test_name}: {reason}")

    test_args = list(args.test_args or [])
    if test_args and test_args[0] == "--":
        test_args = test_args[1:]

    cmd = [str(test_bin), *test_args]
    proc = subprocess.run(cmd, check=False)
    return int(proc.returncode)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
