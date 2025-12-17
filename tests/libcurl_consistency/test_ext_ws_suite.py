"""
可选扩展：WebSocket 低层帧语义一致性（LC-19/LC-20）

显式开启：
  QCURL_LC_EXT=1
"""

from __future__ import annotations

import os
import uuid
from pathlib import Path
from typing import Dict

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import write_json
from tests.libcurl_consistency.pytest_support.case_defs import EXT_WS_CASES
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import ws_observed_for_id
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs
from tests.libcurl_consistency.pytest_support.ws_baseline import run_ws_baseline_case


if os.environ.get("QCURL_LC_EXT", "").strip() != "1":
    pytest.skip("set QCURL_LC_EXT=1 to enable libcurl_consistency ext suite", allow_module_level=True)


def _append_req_id(url: str, req_id: str) -> str:
    sep = "&" if "?" in url else "?"
    return f"{url}{sep}id={req_id}"


def _default_ws_baseline_binary(qt_executable: Path) -> Path:
    return qt_executable.with_name("qcurl_lc_ws_baseline")


@pytest.mark.parametrize("case_id", sorted(EXT_WS_CASES.keys()))
def test_ext_ws_suite(case_id, env, lc_logs, tmp_path):
    case = EXT_WS_CASES[case_id]
    collect_logs = should_collect_service_logs()

    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    ws_baseline_bin = Path(os.environ.get("QCURL_LC_WS_BASELINE", "")).resolve() if os.environ.get("QCURL_LC_WS_BASELINE") else _default_ws_baseline_binary(qt_path)
    if not ws_baseline_bin.exists():
        pytest.skip(f"WS baseline 可执行不存在：{ws_baseline_bin}")

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_{case_id}"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    url_template = str(case["url"]).format(ws_port=env.ws_port)
    baseline_url = _append_req_id(url_template, baseline_req_id)
    qcurl_url = _append_req_id(url_template, qcurl_req_id)

    req_meta = {
        "method": "GET",
        "url": baseline_url,
        "headers": {},
        "body": b"",
    }
    resp_meta = {
        "status": 101,
        "http_version": "ws",
        "headers": {},
        "body": None,
    }

    case_variant = str(case["case"])

    try:
        baseline = run_ws_baseline_case(
            env,
            suite=case["suite"],
            case=case_variant,
            baseline_executable=ws_baseline_bin,
            scenario=str(case["scenario"]),
            url=baseline_url,
            timeout_ms=20000,
            request_meta=req_meta,
            response_meta=resp_meta,
        )

        handshake_log = Path(lc_logs["ws_handshake_log"])
        obs = ws_observed_for_id(handshake_log, baseline_req_id)
        baseline["payload"]["request"]["method"] = obs.method
        baseline["payload"]["request"]["url"] = obs.url
        baseline["payload"]["request"]["headers"] = obs.headers
        write_json(baseline["path"], baseline["payload"])

        case_env: Dict[str, str] = {
            "QCURL_LC_CASE_ID": case_id,
            "QCURL_LC_WS_PORT": str(env.ws_port),
            "QCURL_LC_REQ_ID": qcurl_req_id,
        }
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

        obs = ws_observed_for_id(handshake_log, qcurl_req_id)
        qcurl["payload"]["request"]["method"] = obs.method
        qcurl["payload"]["request"]["url"] = obs.url
        qcurl["payload"]["request"]["headers"] = obs.headers
        write_json(qcurl["path"], qcurl["payload"])

        assert_artifacts_match(baseline["path"], qcurl["path"])
    except Exception:
        if collect_logs:
            collect_service_logs_for_case(
                env,
                suite=case["suite"],
                case=case_variant,
                logs={k: Path(v) for k, v in lc_logs.items() if isinstance(v, Path)},
                meta={
                    "case_id": case_id,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                },
            )
        raise

