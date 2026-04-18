"""
P2：share handle cookie 共享合同。

覆盖默认不共享、显式共享 cookie，以及并发请求下的 cookie 传播一致性。
"""

from __future__ import annotations

import os
import uuid
from pathlib import Path
from typing import Callable, Dict, Iterable, List, Optional
from urllib.parse import parse_qs, urlsplit

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import build_request_semantic, sha256_bytes, write_json
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.observed import observe_http_observed_list_for_id
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


def _cookie_names(summary: str) -> set[str]:
    for token in (summary or "").split():
        if token.startswith("names:"):
            return {item for item in token[len("names:"):].split(",") if item}
    return set()


def _cookie_sha(summary: str) -> str:
    for token in (summary or "").split():
        if token.startswith("sha256:"):
            return token[len("sha256:"):]
    return ""


def _order_cookie_flow(observed_list: Iterable) -> List:
    order = {"/login": 0, "/home": 1}
    return sorted(observed_list, key=lambda obs: order.get(str(obs.url).split("?", 1)[0], 99))


def _order_concurrency_flow(observed_list: Iterable) -> List:
    def key(obs) -> tuple[int, int]:
        path = str(obs.url)
        route = path.split("?", 1)[0]
        if route == "/login":
            return (0, -1)
        seq_raw = parse_qs(urlsplit(path).query).get("seq", ["-1"])[0]
        try:
            seq = int(seq_raw)
        except ValueError:
            seq = -1
        return (1, seq)

    return sorted(observed_list, key=key)


def _responses_from_observed(*, observed_list: Iterable, proto: str,
                             body_for_observed: Callable[[object], bytes]) -> List[Dict]:
    items: List[Dict] = []
    for obs in observed_list:
        body = body_for_observed(obs)
        items.append({
            "status": int(obs.status),
            "http_version": proto,
            "headers": dict(obs.response_headers),
            "body_len": len(body),
            "body_sha256": sha256_bytes(body) if body else "",
        })
    return items


def _requests_from_observed(observed_list: Iterable) -> List[Dict]:
    return [
        build_request_semantic(
            str(obs.method),
            str(obs.url),
            dict(obs.headers),
            b"",
        )
        for obs in observed_list
    ]


def _run_qcurl_case(
    *,
    env,
    qt_path: Path,
    suite: str,
    case: str,
    case_id: str,
    observe_http_port: int,
    proto: str,
    req_id: str,
    share_handle: Optional[str],
    response_meta: Dict,
    download_count: Optional[int],
) -> Dict:
    url = f"http://localhost:{observe_http_port}/login?id={req_id}"
    case_env = {
        "QCURL_LC_CASE_ID": case_id,
        "QCURL_LC_PROTO": proto,
        "QCURL_LC_COUNT": "1",
        "QCURL_LC_DOCNAME": "",
        "QCURL_LC_UPLOAD_SIZE": "0",
        "QCURL_LC_ABORT_OFFSET": "0",
        "QCURL_LC_FILE_SIZE": "0",
        "QCURL_LC_REQ_ID": req_id,
        "QCURL_LC_OBSERVE_HTTP_PORT": str(observe_http_port),
    }
    if share_handle:
        case_env["QCURL_LC_SHARE_HANDLE"] = share_handle

    return run_qt_test(
        env=env,
        suite=suite,
        case=case,
        qt_executable=qt_path,
        args=[],
        request_meta={"method": "GET", "url": url, "headers": {}, "body": b""},
        response_meta=response_meta,
        download_count=download_count,
        case_env=case_env,
    )


def _run_baseline_case(
    *,
    env,
    suite: str,
    case: str,
    mode: str,
    observe_http_port: int,
    proto: str,
    req_id: str,
    response_meta: Dict,
    count: Optional[int] = None,
    download_count: Optional[int] = None,
) -> Dict:
    args = [
        "--mode",
        mode,
        "--proto",
        proto,
        "--port",
        str(observe_http_port),
        "--req-id",
        req_id,
    ]
    if count is not None:
        args.extend(["--count", str(count)])

    return run_libtest_case(
        env=env,
        suite=suite,
        case=case,
        client_name="cli_lc_share_handle",
        args=args,
        request_meta={"method": "GET", "url": f"http://localhost:{observe_http_port}/login?id={req_id}", "headers": {}, "body": b""},
        response_meta=response_meta,
        download_count=download_count,
    )


def _assert_cookie_flow(observed_list: List, *, expect_cookie_on_home: bool, expected_home_status: int) -> None:
    assert len(observed_list) >= 2
    login_obs = observed_list[0]
    home_obs = observed_list[-1]

    assert str(login_obs.url) == "/login"
    assert int(login_obs.status) == 302
    assert "set-cookie" in login_obs.response_headers
    assert "sid" in str(login_obs.response_headers["set-cookie"])

    assert str(home_obs.url).split("?", 1)[0] == "/home"
    assert int(home_obs.status) == expected_home_status
    cookie_summary = str(home_obs.headers.get("cookie") or "")
    if expect_cookie_on_home:
        assert _cookie_names(cookie_summary) == {"sid"}
        assert _cookie_sha(cookie_summary)
    else:
        assert not cookie_summary


def test_p2_share_handle_cookie_flow(env, lc_logs, lc_observe_http):
    qt_path = Path(os.environ["QCURL_QTTEST"])
    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p2_share_handle"
    proto = "http/1.1"

    cases = [
        {
            "case": "lc_p2_share_handle_cookie_disabled",
            "mode": "cookie_disabled",
            "case_id": "p2_share_handle_cookie_disabled",
            "share_handle": None,
            "expected_status": 401,
            "expected_body": b"missing cookie\n",
            "expect_cookie_on_home": False,
        },
        {
            "case": "lc_p2_share_handle_cookie_enabled",
            "mode": "cookie_enabled",
            "case_id": "p2_share_handle_cookie_enabled",
            "share_handle": "cookie",
            "expected_status": 200,
            "expected_body": b"home-ok\n",
            "expect_cookie_on_home": True,
        },
    ]

    for cfg in cases:
        trace_base = f"lc_{uuid.uuid4().hex[:8]}_{cfg['mode']}"
        baseline_req_id = f"{trace_base}__baseline"
        qcurl_req_id = f"{trace_base}__qcurl"
        response_meta = {
            "status": int(cfg["expected_status"]),
            "http_version": proto,
            "headers": {},
            "body": cfg["expected_body"],
        }

        try:
            observe_log.write_text("", encoding="utf-8")
            baseline = _run_baseline_case(
                env=env,
                suite=suite,
                case=str(cfg["case"]),
                mode=str(cfg["mode"]),
                observe_http_port=port,
                proto=proto,
                req_id=baseline_req_id,
                response_meta=response_meta,
                download_count=1,
            )

            baseline_obs = _order_cookie_flow(
                observe_http_observed_list_for_id(observe_log, baseline_req_id, expected_count=2)
            )
            _assert_cookie_flow(
                baseline_obs,
                expect_cookie_on_home=bool(cfg["expect_cookie_on_home"]),
                expected_home_status=int(cfg["expected_status"]),
            )
            baseline["payload"]["requests"] = _requests_from_observed(baseline_obs)
            baseline["payload"]["responses"] = _responses_from_observed(
                observed_list=baseline_obs,
                proto=proto,
                body_for_observed=lambda obs, expected=cfg["expected_body"]: b""
                if str(obs.url) == "/login" else expected,
            )
            baseline["payload"]["request"] = baseline["payload"]["requests"][0]
            baseline["payload"]["response"] = baseline["payload"]["responses"][-1]
            write_json(baseline["path"], baseline["payload"])

            observe_log.write_text("", encoding="utf-8")
            qcurl = _run_qcurl_case(
                env=env,
                qt_path=qt_path,
                suite=suite,
                case=str(cfg["case"]),
                case_id=str(cfg["case_id"]),
                observe_http_port=port,
                proto=proto,
                req_id=qcurl_req_id,
                share_handle=cfg["share_handle"],
                response_meta=response_meta,
                download_count=1,
            )

            qcurl_obs = _order_cookie_flow(
                observe_http_observed_list_for_id(observe_log, qcurl_req_id, expected_count=2)
            )
            _assert_cookie_flow(
                qcurl_obs,
                expect_cookie_on_home=bool(cfg["expect_cookie_on_home"]),
                expected_home_status=int(cfg["expected_status"]),
            )
            qcurl["payload"]["requests"] = _requests_from_observed(qcurl_obs)
            qcurl["payload"]["responses"] = _responses_from_observed(
                observed_list=qcurl_obs,
                proto=proto,
                body_for_observed=lambda obs, expected=cfg["expected_body"]: b""
                if str(obs.url) == "/login" else expected,
            )
            qcurl["payload"]["request"] = qcurl["payload"]["requests"][0]
            qcurl["payload"]["response"] = qcurl["payload"]["responses"][-1]
            write_json(qcurl["path"], qcurl["payload"])

            assert_artifacts_match(baseline["path"], qcurl["path"])
        except Exception:
            if collect_logs:
                collect_service_logs_for_case(
                    env,
                    suite=suite,
                    case=str(cfg["case"]),
                    logs={**lc_logs, "observe_http_log": observe_log},
                    meta={
                        "case_id": str(cfg["case_id"]),
                        "proto": proto,
                        "observe_http_port": port,
                        "baseline_req_id": baseline_req_id,
                        "qcurl_req_id": qcurl_req_id,
                    },
                )
            raise


def test_p2_share_handle_concurrency_contract(env, lc_logs, lc_observe_http):
    qt_path = Path(os.environ["QCURL_QTTEST"])
    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p2_share_handle"
    case = "lc_p2_share_handle_cookie_concurrency"
    case_id = "p2_share_handle_cookie_concurrency"
    proto = "http/1.1"
    concurrency = 64
    home_body = b"home-ok\n"
    response_meta = {
        "status": 200,
        "http_version": proto,
        "headers": {},
        "body": home_body,
    }

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_share_concurrency"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    try:
        observe_log.write_text("", encoding="utf-8")
        baseline = _run_baseline_case(
            env=env,
            suite=suite,
            case=case,
            mode="cookie_concurrency",
            observe_http_port=port,
            proto=proto,
            req_id=baseline_req_id,
            response_meta=response_meta,
            count=concurrency,
            download_count=None,
        )

        baseline_obs = _order_concurrency_flow(
            observe_http_observed_list_for_id(observe_log, baseline_req_id, expected_count=concurrency + 1)
        )
        _assert_cookie_flow(baseline_obs[:2], expect_cookie_on_home=True, expected_home_status=200)
        assert len(baseline_obs) == concurrency + 1
        for obs in baseline_obs[1:]:
            assert str(obs.url).startswith("/home?seq=")
            assert int(obs.status) == 200
            cookie_summary = str(obs.headers.get("cookie") or "")
            assert _cookie_names(cookie_summary) == {"sid"}
            assert _cookie_sha(cookie_summary)
        baseline["payload"]["requests"] = _requests_from_observed(baseline_obs)
        baseline["payload"]["responses"] = _responses_from_observed(
            observed_list=baseline_obs,
            proto=proto,
            body_for_observed=lambda obs: b"" if str(obs.url) == "/login" else home_body,
        )
        baseline["payload"]["request"] = baseline["payload"]["requests"][0]
        baseline["payload"]["response"] = baseline["payload"]["responses"][-1]
        write_json(baseline["path"], baseline["payload"])

        observe_log.write_text("", encoding="utf-8")
        qcurl = _run_qcurl_case(
            env=env,
            qt_path=qt_path,
            suite=suite,
            case=case,
            case_id=case_id,
            observe_http_port=port,
            proto=proto,
            req_id=qcurl_req_id,
            share_handle="cookie",
            response_meta=response_meta,
            download_count=None,
        )

        qcurl_obs = _order_concurrency_flow(
            observe_http_observed_list_for_id(observe_log, qcurl_req_id, expected_count=concurrency + 1)
        )
        _assert_cookie_flow(qcurl_obs[:2], expect_cookie_on_home=True, expected_home_status=200)
        assert len(qcurl_obs) == concurrency + 1
        for obs in qcurl_obs[1:]:
            assert str(obs.url).startswith("/home?seq=")
            assert int(obs.status) == 200
            cookie_summary = str(obs.headers.get("cookie") or "")
            assert _cookie_names(cookie_summary) == {"sid"}
            assert _cookie_sha(cookie_summary)
        qcurl["payload"]["requests"] = _requests_from_observed(qcurl_obs)
        qcurl["payload"]["responses"] = _responses_from_observed(
            observed_list=qcurl_obs,
            proto=proto,
            body_for_observed=lambda obs: b"" if str(obs.url) == "/login" else home_body,
        )
        qcurl["payload"]["request"] = qcurl["payload"]["requests"][0]
        qcurl["payload"]["response"] = qcurl["payload"]["responses"][-1]
        write_json(qcurl["path"], qcurl["payload"])

        assert_artifacts_match(baseline["path"], qcurl["path"])
    except Exception:
        if collect_logs:
            collect_service_logs_for_case(
                env,
                suite=suite,
                case=case,
                logs={**lc_logs, "observe_http_log": observe_log},
                meta={
                    "case_id": case_id,
                    "proto": proto,
                    "observe_http_port": port,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "concurrency": concurrency,
                },
            )
        raise
