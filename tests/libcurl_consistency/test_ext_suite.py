"""
可选扩展套件（LC-11）：
- 默认不作为 Gate，避免波动。
- 显式开启：QCURL_LC_EXT=1

当前扩展覆盖：
- 并发下载压力（h2/h3）：用于覆盖 multiplexing/调度路径与数据一致性。
"""

from __future__ import annotations

import os
import uuid
from pathlib import Path
from typing import Dict, List

import pytest

from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.case_defs import EXT_CASES
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.artifacts import sha256_file, write_json
from tests.libcurl_consistency.pytest_support.observed import (
    httpd_observed_for_id,
    httpd_observed_list_for_id,
    nghttpx_observed_for_id,
    nghttpx_observed_list_for_id,
)
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


if os.environ.get("QCURL_LC_EXT", "").strip() != "1":
    pytest.skip("set QCURL_LC_EXT=1 to enable libcurl_consistency ext suite", allow_module_level=True)


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

def _download_files_for_baseline(env, client_name: str, count: int) -> List[Path]:
    run_dir = Path(env.gen_dir) / client_name
    return [run_dir / f"download_{i}.data" for i in range(count)]

def _download_files_for_qcurl(qcurl_artifact_path: Path, count: int) -> List[Path]:
    run_dir = qcurl_artifact_path.parent / "qcurl_run"
    return [run_dir / f"download_{i}.data" for i in range(count)]

def _response_items_from_files(files: List[Path], *, proto: str, status: int, urls: List[str]) -> List[Dict]:
    items: List[Dict] = []
    for idx, f in enumerate(files):
        body_len, body_sha256 = sha256_file(f)
        items.append({
            "status": status,
            "http_version": proto,
            "headers": {},
            "body_len": body_len,
            "body_sha256": body_sha256,
            "url": urls[idx] if idx < len(urls) else "",
        })
    return items


@pytest.mark.parametrize("case_id", sorted(EXT_CASES.keys()))
def test_ext_suite(case_id, env, lc_services, lc_logs, tmp_path):
    case = EXT_CASES[case_id]
    collect_logs = should_collect_service_logs()
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    protos = ["h2"]
    if env.have_h3():
        protos.append("h3")
    if case.get("protos"):
        protos = [p for p in case["protos"] if (p != "h3" or env.have_h3())]
        if not protos:
            pytest.skip("ext case requires h3 but env.have_h3() is false")

    for proto in protos:
        trace_base = f"lc_{uuid.uuid4().hex[:8]}_{case_id}_{proto}"
        baseline_req_id = f"{trace_base}__baseline"
        qcurl_req_id = f"{trace_base}__qcurl"

        resolved_defaults = dict(case["defaults"])
        resolved_defaults["proto"] = proto
        resolved_defaults["req_id"] = baseline_req_id
        if "url" in resolved_defaults:
            resolved_defaults["url"] = str(resolved_defaults["url"]).format(
                https_port=env.https_port,
                ws_port=env.ws_port,
            )
            resolved_defaults["url"] = _append_req_id(resolved_defaults["url"], baseline_req_id)
        if "url_prefix" in resolved_defaults:
            resolved_defaults["url_prefix"] = str(resolved_defaults["url_prefix"]).format(
                https_port=env.https_port,
                ws_port=env.ws_port,
            )

        args = _fmt_args(case["args_template"], resolved_defaults, env)
        req_url = resolved_defaults.get("url")
        if not req_url and resolved_defaults.get("url_prefix"):
            req_url = _append_req_id(f"{resolved_defaults['url_prefix']}0001", baseline_req_id)
        req_meta = {
            "method": "GET",
            "url": req_url,
            "headers": {},
            "body": b"",
        }
        resp_meta = {
            "status": 200,
            "http_version": proto,
            "headers": {},
            "body": None,
        }

        case_variant = f"{case['case']}_{proto}"
        case_env = {
            "QCURL_LC_CASE_ID": case_id,
            "QCURL_LC_PROTO": proto,
            "QCURL_LC_HTTPS_PORT": str(env.https_port),
            "QCURL_LC_WS_PORT": str(env.ws_port),
            "QCURL_LC_COUNT": str(resolved_defaults.get("count", 1)),
            "QCURL_LC_DOCNAME": str(resolved_defaults.get("docname", "")),
            "QCURL_LC_UPLOAD_SIZE": "0",
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

            expected_requests = int(case.get("expected_requests") or 0)
            if expected_requests > 1:
                if proto == "h3":
                    access_log = Path(lc_logs["nghttpx_access_log"])
                    obs_list = nghttpx_observed_list_for_id(access_log, baseline_req_id, expected_count=expected_requests)
                else:
                    access_log = Path(lc_logs["httpd_access_log"])
                    obs_list = httpd_observed_list_for_id(access_log, baseline_req_id, expected_count=expected_requests)
                assert all(o.http_version == proto for o in obs_list)
                assert all(o.status == 200 for o in obs_list)
                baseline["payload"]["requests"] = [{
                    "method": o.method,
                    "url": o.url,
                    "headers": o.headers,
                    "body_len": 0,
                    "body_sha256": "",
                } for o in obs_list]
                baseline["payload"]["request"]["method"] = obs_list[0].method
                baseline["payload"]["request"]["url"] = obs_list[0].url
                baseline["payload"]["request"]["headers"] = obs_list[0].headers
                baseline["payload"]["response"]["status"] = obs_list[0].status
                baseline["payload"]["response"]["http_version"] = obs_list[0].http_version
                baseline_files = _download_files_for_baseline(env, case["client"], expected_requests)
                baseline["payload"]["responses"] = _response_items_from_files(
                    baseline_files,
                    proto=proto,
                    status=200,
                    urls=[o.url for o in obs_list],
                )
            else:
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

            if expected_requests > 1:
                if proto == "h3":
                    access_log = Path(lc_logs["nghttpx_access_log"])
                    obs_list = nghttpx_observed_list_for_id(access_log, qcurl_req_id, expected_count=expected_requests)
                else:
                    access_log = Path(lc_logs["httpd_access_log"])
                    obs_list = httpd_observed_list_for_id(access_log, qcurl_req_id, expected_count=expected_requests)
                assert all(o.http_version == proto for o in obs_list)
                assert all(o.status == 200 for o in obs_list)
                qcurl["payload"]["requests"] = [{
                    "method": o.method,
                    "url": o.url,
                    "headers": o.headers,
                    "body_len": 0,
                    "body_sha256": "",
                } for o in obs_list]
                qcurl["payload"]["request"]["method"] = obs_list[0].method
                qcurl["payload"]["request"]["url"] = obs_list[0].url
                qcurl["payload"]["request"]["headers"] = obs_list[0].headers
                qcurl["payload"]["response"]["status"] = obs_list[0].status
                qcurl["payload"]["response"]["http_version"] = obs_list[0].http_version
                qcurl_files = _download_files_for_qcurl(qcurl["path"], expected_requests)
                qcurl["payload"]["responses"] = _response_items_from_files(
                    qcurl_files,
                    proto=proto,
                    status=200,
                    urls=[o.url for o in obs_list],
                )
            else:
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
