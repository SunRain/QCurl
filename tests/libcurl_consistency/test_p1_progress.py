"""
P1：进度摘要一致性。

只比较稳定摘要字段，不比较事件次数、时间戳或瞬时速率。
"""

from __future__ import annotations

import json
import os
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import artifacts_root, ensure_case_dir, write_json
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


def _load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def test_p1_progress_download_h2(env, lc_logs, tmp_path):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("当前环境未提供 QCURL_QTTEST 可执行文件，跳过该用例")

    collect_logs = should_collect_service_logs()
    suite = "p1_progress"
    proto = "h2"
    case_variant = "p1_progress_download_h2"

    url = f"https://localhost:{int(env.https_port)}/data-1m"
    resp_meta = {"status": 200, "http_version": proto, "headers": {}, "body": None}

    case_dir = ensure_case_dir(artifacts_root(env), suite=suite, case=case_variant)
    baseline_progress = case_dir / "baseline_progress_summary.json"

    try:
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_http",
            args=[
                "-V",
                proto,
                "--progress-out",
                str(baseline_progress),
                url,
            ],
            request_meta={"method": "GET", "url": url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
        )
        baseline["payload"]["progress_summary"] = _load_json(baseline_progress)
        write_json(baseline["path"], baseline["payload"])

        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            args=[],
            request_meta={"method": "GET", "url": url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
            case_env={
                "QCURL_LC_CASE_ID": "p1_progress_download",
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_HTTPS_PORT": str(int(env.https_port)),
                "QCURL_LC_DOCNAME": "data-1m",
            },
        )
        qcurl_progress = qcurl["path"].parent / "qcurl_run" / "progress_summary.json"
        qcurl["payload"]["progress_summary"] = _load_json(qcurl_progress)
        write_json(qcurl["path"], qcurl["payload"])

        # 进度摘要终值应对齐响应 body_len（该场景下可由 Content-Length 稳定确定）。
        expected_len = int(baseline["payload"]["response"]["body_len"])
        assert expected_len > 0
        assert baseline["payload"]["progress_summary"]["download"]["now_max"] == expected_len
        assert baseline["payload"]["progress_summary"]["download"]["total_max"] == expected_len
        assert qcurl["payload"]["progress_summary"]["download"]["now_max"] == expected_len
        assert qcurl["payload"]["progress_summary"]["download"]["total_max"] == expected_len

        assert_artifacts_match(baseline["path"], qcurl["path"])
    except Exception:
        if collect_logs:
            collect_service_logs_for_case(
                env,
                suite=suite,
                case=case_variant,
                logs=lc_logs,
                meta={
                    "case_id": "p1_progress_download",
                    "proto": proto,
                    "url": url,
                },
            )
        raise


def test_p1_progress_upload_h2(env, lc_logs, tmp_path):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("当前环境未提供 QCURL_QTTEST 可执行文件，跳过该用例")

    collect_logs = should_collect_service_logs()
    suite = "p1_progress"
    proto = "h2"
    case_variant = "p1_progress_upload_h2"

    upload_size = 128 * 1024
    body = b"x" * upload_size
    url = f"https://localhost:{int(env.https_port)}/curltest/echo"
    resp_meta = {"status": 200, "http_version": proto, "headers": {}, "body": None}

    case_dir = ensure_case_dir(artifacts_root(env), suite=suite, case=case_variant)
    baseline_progress = case_dir / "baseline_progress_summary.json"

    try:
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_http",
            args=[
                "-V",
                proto,
                "--method",
                "POST",
                "--data-size",
                str(upload_size),
                "--progress-out",
                str(baseline_progress),
                url,
            ],
            request_meta={"method": "POST", "url": url, "headers": {}, "body": body},
            response_meta=resp_meta,
            download_count=1,
        )
        baseline["payload"]["progress_summary"] = _load_json(baseline_progress)
        write_json(baseline["path"], baseline["payload"])

        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            args=[],
            request_meta={"method": "POST", "url": url, "headers": {}, "body": body},
            response_meta=resp_meta,
            download_count=1,
            case_env={
                "QCURL_LC_CASE_ID": "p1_progress_upload",
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_HTTPS_PORT": str(int(env.https_port)),
                "QCURL_LC_UPLOAD_SIZE": str(upload_size),
            },
        )
        qcurl_progress = qcurl["path"].parent / "qcurl_run" / "progress_summary.json"
        qcurl["payload"]["progress_summary"] = _load_json(qcurl_progress)
        write_json(qcurl["path"], qcurl["payload"])

        # 上传进度摘要的 now_max/total_max 应对齐请求体大小。
        assert baseline["payload"]["progress_summary"]["upload"]["now_max"] == upload_size
        assert baseline["payload"]["progress_summary"]["upload"]["total_max"] == upload_size
        assert qcurl["payload"]["progress_summary"]["upload"]["now_max"] == upload_size
        assert qcurl["payload"]["progress_summary"]["upload"]["total_max"] == upload_size

        assert_artifacts_match(baseline["path"], qcurl["path"])
    except Exception:
        if collect_logs:
            collect_service_logs_for_case(
                env,
                suite=suite,
                case=case_variant,
                logs=lc_logs,
                meta={
                    "case_id": "p1_progress_upload",
                    "proto": proto,
                    "url": url,
                    "upload_size": upload_size,
                },
            )
        raise
