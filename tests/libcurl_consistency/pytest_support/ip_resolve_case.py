"""IP 族选择 paired case 的共享执行器。"""

from __future__ import annotations

import ipaddress
import uuid
from pathlib import Path

from .artifacts import write_json
from .baseline import run_libtest_case
from .compare import assert_artifacts_match
from .observed import observe_http_observed_for_id
from .qcurl_runner import require_qcurl_qttest, run_qt_test


def run_ip_resolve_case(env, server: dict[str, object], mode: str) -> None:
    """执行 libcurl/QCurl IP 族选择并比较服务端真实 peer 地址族。"""

    port = int(server["port"])
    observe_log = Path(str(server["log_file"]))
    trace = f"lc_{uuid.uuid4().hex[:8]}_ip_{mode}"
    baseline_id = f"{trace}__baseline"
    qcurl_id = f"{trace}__qcurl"
    base_url = f"http://localhost:{port}/cookie"
    suite = "p2_ip_resolve"
    case_variant = f"lc_ip_resolve_{mode}"
    response = {
        "status": 200,
        "http_version": "http/1.1",
        "headers": {},
        "body": None,
    }

    baseline = run_libtest_case(
        env=env,
        suite=suite,
        case=case_variant,
        client_name="cli_lc_http",
        args=["-V", "http/1.1", "--ip-resolve", mode, f"{base_url}?id={baseline_id}"],
        request_meta={"method": "GET", "url": base_url, "headers": {}, "body": b""},
        response_meta=response,
        download_count=1,
    )
    qcurl = run_qt_test(
        env=env,
        suite=suite,
        case=case_variant,
        qt_executable=require_qcurl_qttest(),
        request_meta={"method": "GET", "url": base_url, "headers": {}, "body": b""},
        response_meta=response,
        download_count=1,
        case_env={
            "QCURL_LC_CASE_ID": f"p2_ip_resolve_{mode}",
            "QCURL_LC_PROTO": "http/1.1",
            "QCURL_LC_REQ_ID": qcurl_id,
            "QCURL_LC_TARGET_URL": f"{base_url}?id={qcurl_id}",
            "QCURL_LC_IP_RESOLVE": mode,
        },
    )

    baseline_observed = observe_http_observed_for_id(observe_log, baseline_id)
    qcurl_observed = observe_http_observed_for_id(observe_log, qcurl_id)
    expected_family = 4 if mode == "v4" else 6
    baseline_family = ipaddress.ip_address(baseline_observed.peer.rsplit(":", 1)[0]).version
    qcurl_family = ipaddress.ip_address(qcurl_observed.peer.rsplit(":", 1)[0]).version
    assert baseline_family == expected_family
    assert qcurl_family == expected_family
    baseline["payload"]["transport"] = {"ip_resolve": mode, "peer_family": baseline_family}
    qcurl["payload"]["transport"] = {"ip_resolve": mode, "peer_family": qcurl_family}
    write_json(baseline["path"], baseline["payload"])
    write_json(qcurl["path"], qcurl["payload"])
    assert_artifacts_match(baseline["path"], qcurl["path"])
