"""
P2：上传 READFUNC_PAUSE 自动恢复一致性（可观测数据层面最小合同）。

判定核心：
- 服务端观测到请求体字节一致（len/hash）
- 回显响应体字节一致（len/hash）
- 两侧均观测到 “read=0 非 EOF” 的空窗（zero_read_count > 0）
"""

from __future__ import annotations

import json
import os
import uuid
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import artifacts_root, ensure_case_dir, sha256_bytes, write_json
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import observe_http_observed_list_for_id
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


def _load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def _normalize_req_headers(headers: dict) -> dict:
    out: dict = {}
    for name in ("host", "content-length", "transfer-encoding", "expect"):
        v = headers.get(name)
        if v:
            out[name] = v
    return out


def test_p2_upload_readfunc_pause_resume_http_1_1(env, lc_observe_http):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p2_upload_pause_resume"
    proto = "http/1.1"
    case_id = "p2_upload_readfunc_pause_resume"
    case_variant = "p2_upload_readfunc_pause_resume_http_1.1"

    upload_size = 64 * 1024
    body = b"u" * upload_size

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_{case_id}"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    base_url = f"http://localhost:{port}/method"
    baseline_url = f"{base_url}?id={baseline_req_id}"
    qcurl_url = f"{base_url}?id={qcurl_req_id}"

    resp_meta = {"status": 200, "http_version": proto, "headers": {}, "body": None}

    case_dir = ensure_case_dir(artifacts_root(env), suite=suite, case=case_variant)
    baseline_events = case_dir / "baseline_upload_pause_resume.json"

    try:
        observe_log.write_text("", encoding="utf-8")
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_upload_pause_resume",
            args=[
                "-V",
                proto,
                "--payload-size",
                str(upload_size),
                "--events-out",
                str(baseline_events),
                baseline_url,
            ],
            request_meta={"method": "PUT", "url": baseline_url, "headers": {}, "body": body},
            response_meta=resp_meta,
            download_count=1,
            allowed_exit_codes={0},
        )
        obs_base = observe_http_observed_list_for_id(observe_log, baseline_req_id, expected_count=1)
        baseline["payload"]["requests"] = [{
            "method": obs_base[0].method,
            "url": obs_base[0].url,
            "headers": _normalize_req_headers(obs_base[0].headers),
            "body_len": len(body),
            "body_sha256": sha256_bytes(body),
        }]
        baseline["payload"]["responses"] = [{
            **baseline["payload"]["response"],
            "status": obs_base[0].status,
            "http_version": proto,
            "headers": dict(obs_base[0].response_headers),
        }]
        baseline["payload"]["request"]["method"] = obs_base[0].method
        baseline["payload"]["request"]["url"] = obs_base[0].url
        baseline["payload"]["request"]["headers"] = _normalize_req_headers(obs_base[0].headers)
        baseline["payload"]["response"]["status"] = obs_base[0].status
        baseline["payload"]["response"]["http_version"] = proto
        baseline["payload"]["response"]["headers"] = dict(obs_base[0].response_headers)
        baseline["payload"]["upload_pause_resume"] = _load_json(baseline_events)
        write_json(baseline["path"], baseline["payload"])

        observe_log.write_text("", encoding="utf-8")
        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            args=[],
            request_meta={"method": "PUT", "url": qcurl_url, "headers": {}, "body": body},
            response_meta=resp_meta,
            download_count=1,
            case_env={
                "QCURL_LC_CASE_ID": case_id,
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_OBSERVE_HTTP_PORT": str(port),
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_UPLOAD_SIZE": str(upload_size),
            },
        )

        obs_q = observe_http_observed_list_for_id(observe_log, qcurl_req_id, expected_count=1)
        qcurl["payload"]["requests"] = [{
            "method": obs_q[0].method,
            "url": obs_q[0].url,
            "headers": _normalize_req_headers(obs_q[0].headers),
            "body_len": len(body),
            "body_sha256": sha256_bytes(body),
        }]
        qcurl["payload"]["responses"] = [{
            **qcurl["payload"]["response"],
            "status": obs_q[0].status,
            "http_version": proto,
            "headers": dict(obs_q[0].response_headers),
        }]
        qcurl["payload"]["request"]["method"] = obs_q[0].method
        qcurl["payload"]["request"]["url"] = obs_q[0].url
        qcurl["payload"]["request"]["headers"] = _normalize_req_headers(obs_q[0].headers)
        qcurl["payload"]["response"]["status"] = obs_q[0].status
        qcurl["payload"]["response"]["http_version"] = proto
        qcurl["payload"]["response"]["headers"] = dict(obs_q[0].response_headers)

        qcurl_events_path = qcurl["path"].parent / "qcurl_run" / "upload_pause_resume.json"
        qcurl["payload"]["upload_pause_resume"] = _load_json(qcurl_events_path)
        write_json(qcurl["path"], qcurl["payload"])

        assert_artifacts_match(baseline["path"], qcurl["path"])
    except Exception:
        if collect_logs:
            collect_service_logs_for_case(
                env,
                suite=suite,
                case=case_variant,
                logs={"observe_http_log": observe_log},
                meta={
                    "case_id": case_id,
                    "proto": proto,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "observe_port": port,
                    "upload_size": upload_size,
                },
            )
        raise
