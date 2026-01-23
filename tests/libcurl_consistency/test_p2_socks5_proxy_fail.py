"""
P2：SOCKS5 proxy 失败语义一致性（LC-39，可选）。

目标：
- baseline 与 QCurl 均通过 SOCKS5 代理发起请求，并稳定触发 proxy 失败语义
- 对齐可观测输出：失败终态（error.kind=proxy）、curlcode=97、落盘 body 为空

参考 upstream：curl/tests/data/test703（Expect errorcode=97）

代理：repo 内置 socks5_proxy_server.py（固定返回 REP=0x01）
基线：repo 内置 qcurl_lc_http_baseline（--proxy-type socks5）
QCurl：tst_LibcurlConsistency（p2_socks5_proxy_connect_fail）
"""

from __future__ import annotations

import os
import re
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import apply_error_namespaces, write_json
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


_CURLINFO_RE = re.compile(r"curlcode=(\d+)\s+http_code=(\d+)")


def _parse_curlcode_http_code(stderr_lines: list[str]) -> tuple[int, int]:
    for line in stderr_lines:
        m = _CURLINFO_RE.search(line)
        if m:
            return int(m.group(1)), int(m.group(2))
    return -1, -1


def _log_lines_count(path: Path) -> int:
    if not path.exists():
        return 0
    return sum(1 for raw in path.read_text(encoding="utf-8", errors="replace").splitlines() if raw.strip())


def test_p2_socks5_proxy_connect_fail(env, lc_logs, lc_socks5_proxy):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    collect_logs = should_collect_service_logs()

    proxy_port = int(lc_socks5_proxy["port"])
    proxy_log = Path(str(lc_socks5_proxy["log_file"]))

    suite = "p2_socks5"
    proto = "http/1.1"
    case_variant = "p2_socks5_proxy_connect_fail_http_1.1"

    # 目标 URL 不要求真实可达：stub proxy 会在握手阶段直接返回失败（不会连接目标）。
    target_url = "http://127.0.0.1:1/"
    proxy = f"127.0.0.1:{proxy_port}"
    resp_meta = {"status": 0, "http_version": proto, "headers": {}, "body": None}

    try:
        proxy_log.write_text("", encoding="utf-8")
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_http",
            args=[
                "-V",
                proto,
                "--proxy",
                proxy,
                "--proxy-type",
                "socks5",
                target_url,
            ],
            request_meta={"method": "GET", "url": target_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
            allowed_exit_codes={0, 7},
        )
        curlcode, http_code = _parse_curlcode_http_code(list(baseline["payload"].get("stderr") or []))
        if curlcode < 0:
            raise AssertionError("baseline stderr 未包含 curlcode/http_code")
        assert curlcode == 97, f"unexpected curlcode: {curlcode}"
        apply_error_namespaces(
            baseline["payload"],
            kind="proxy",
            http_status=0,
            curlcode=curlcode,
            http_code=http_code,
        )
        write_json(baseline["path"], baseline["payload"])
        assert _log_lines_count(proxy_log) >= 1, "baseline 未触发 SOCKS5 stub 连接（proxy log 为空）"

        proxy_log.write_text("", encoding="utf-8")
        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            args=[],
            request_meta={"method": "GET", "url": target_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
            case_env={
                "QCURL_LC_CASE_ID": "p2_socks5_proxy_connect_fail",
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_TARGET_URL": target_url,
                "QCURL_LC_SOCKS5_PORT": str(proxy_port),
            },
        )
        apply_error_namespaces(
            qcurl["payload"],
            kind="proxy",
            http_status=0,
            curlcode=97,
            http_code=0,
        )
        write_json(qcurl["path"], qcurl["payload"])
        assert _log_lines_count(proxy_log) >= 1, "QCurl 未触发 SOCKS5 stub 连接（proxy log 为空）"

        assert_artifacts_match(baseline["path"], qcurl["path"])
    except Exception:
        if collect_logs:
            logs = dict(lc_logs)
            logs["socks5_proxy_log"] = proxy_log
            collect_service_logs_for_case(
                env,
                suite=suite,
                case=case_variant,
                logs=logs,
                meta={
                    "case_id": "p2_socks5_proxy_connect_fail",
                    "case_variant": case_variant,
                    "proto": proto,
                    "target_url": target_url,
                    "proxy_port": proxy_port,
                },
            )
        raise
