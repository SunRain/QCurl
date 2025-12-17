"""
P1：CURLOPT_POSTFIELDS 二进制（含 \\0）一致性（LC-9）。

基线：repo 内置 `qcurl_lc_postfields_binary_baseline`（对齐 test1531 语义）
QCurl：tst_LibcurlConsistency 通过 QCURL_LC_CASE_ID 执行相同 payload
"""

from __future__ import annotations

import os
import uuid
from pathlib import Path
from typing import Dict, List

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import write_json
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.case_defs import P1_CASES
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import httpd_observed_for_id, nghttpx_observed_for_id
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


def _fmt_args(template: List[str], defaults: Dict, env) -> List[str]:
    ctx = defaults.copy()
    ctx.update({
        "https_port": env.https_port,
        "ws_port": env.ws_port,
    })
    return [str(x).format(**ctx) for x in template]


def _append_req_id(url: str, req_id: str) -> str:
    sep = "&" if "?" in url else "?"
    return f"{url}{sep}id={req_id}"


def _postfields_binary_payload() -> bytes:
    return b".abc\x00xyz"


@pytest.mark.parametrize("case_id", sorted(P1_CASES.keys()))
def test_p1_postfields_binary(case_id, env, lc_services, lc_logs, tmp_path):
    case = P1_CASES[case_id]
    collect_logs = should_collect_service_logs()
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    http_protos = ["http/1.1", "h2"]
    if env.have_h3():
        http_protos.append("h3")

    for proto in http_protos:
        trace_base = f"lc_{uuid.uuid4().hex[:8]}_{case_id}_{proto.replace('/', '_')}"
        baseline_req_id = f"{trace_base}__baseline"
        qcurl_req_id = f"{trace_base}__qcurl"

        resolved_defaults = dict(case["defaults"])
        resolved_defaults["proto"] = proto
        resolved_defaults["url"] = str(resolved_defaults["url"]).format(
            https_port=env.https_port,
            ws_port=env.ws_port,
        )
        resolved_defaults["url"] = _append_req_id(resolved_defaults["url"], baseline_req_id)

        args = _fmt_args(case["args_template"], resolved_defaults, env)
        body = _postfields_binary_payload()
        req_meta = {
            "method": "POST",
            "url": resolved_defaults["url"],
            "headers": {"Content-Length": str(len(body))},
            "body": body,
        }
        resp_meta = {
            "status": 200,
            "http_version": proto,
            "headers": {},
            "body": None,
        }

        case_variant = f"{case['case']}_{proto.replace('/', '_')}"
        case_env = {
            "QCURL_LC_CASE_ID": case_id,
            "QCURL_LC_PROTO": str(resolved_defaults.get("proto", "h2")),
            "QCURL_LC_HTTPS_PORT": str(env.https_port),
            "QCURL_LC_WS_PORT": str(env.ws_port),
            "QCURL_LC_COUNT": "1",
            "QCURL_LC_DOCNAME": "",
            "QCURL_LC_UPLOAD_SIZE": str(len(body)),
            "QCURL_LC_ABORT_OFFSET": "0",
            "QCURL_LC_FILE_SIZE": "0",
            "QCURL_LC_REQ_ID": qcurl_req_id,
        }

        try:
            try:
                baseline = run_libtest_case(
                    env=env,
                    suite=case["suite"],
                    case=case_variant,
                    client_name=case["client"],
                    args=args,
                    request_meta=req_meta,
                    response_meta=resp_meta,
                    download_count=case.get("baseline_download_count"),
                )
            except FileNotFoundError as exc:
                pytest.skip(f"libtests 未构建: {exc}")

            if proto == "h3":
                access_log = Path(lc_logs["nghttpx_access_log"])
                obs = nghttpx_observed_for_id(access_log, baseline_req_id, require_range=False)
            else:
                access_log = Path(lc_logs["httpd_access_log"])
                obs = httpd_observed_for_id(access_log, baseline_req_id, require_range=False)
            assert obs.http_version == proto
            baseline["payload"]["request"]["method"] = obs.method
            baseline["payload"]["request"]["url"] = obs.url
            baseline["payload"]["request"]["headers"] = obs.headers
            baseline["payload"]["response"]["status"] = obs.status
            baseline["payload"]["response"]["http_version"] = obs.http_version
            write_json(baseline["path"], baseline["payload"])

            qcurl = run_qt_test(
                env=env,
                suite=case["suite"],
                case=case_variant,
                qt_executable=qt_path,
                args=[],
                request_meta=req_meta,
                response_meta=resp_meta,
                download_files=None,
                download_count=case.get("qcurl_download_count"),
                case_env=case_env,
            )

            if proto == "h3":
                access_log = Path(lc_logs["nghttpx_access_log"])
                obs = nghttpx_observed_for_id(access_log, qcurl_req_id, require_range=False)
            else:
                access_log = Path(lc_logs["httpd_access_log"])
                obs = httpd_observed_for_id(access_log, qcurl_req_id, require_range=False)
            assert obs.http_version == proto
            qcurl["payload"]["request"]["method"] = obs.method
            qcurl["payload"]["request"]["url"] = obs.url
            qcurl["payload"]["request"]["headers"] = obs.headers
            qcurl["payload"]["response"]["status"] = obs.status
            qcurl["payload"]["response"]["http_version"] = obs.http_version
            write_json(qcurl["path"], qcurl["payload"])

            assert_artifacts_match(baseline["path"], qcurl["path"])
        except Exception:
            if collect_logs:
                collect_service_logs_for_case(
                    env,
                    suite=case["suite"],
                    case=case_variant,
                    logs=lc_logs,
                    meta={
                        "case_id": case_id,
                        "case_variant": case_variant,
                        "proto": proto,
                        "baseline_req_id": baseline_req_id,
                        "qcurl_req_id": qcurl_req_id,
                    },
                )
            raise
