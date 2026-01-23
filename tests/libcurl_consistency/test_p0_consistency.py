"""
P0 一致性回归骨架：
- 基于 case_defs 定义的参数模板运行 libcurl baseline 与 QCurl Qt Test。
- 当前阶段：若缺少 httpd/nghttpx/config.ini/Qt Test 可执行，将跳过用例，提供明确原因。
"""

from __future__ import annotations

import os
from pathlib import Path
from typing import Dict, List
import uuid

import pytest

from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.case_defs import P0_CASES
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.artifacts import sha256_file, write_json
from tests.libcurl_consistency.pytest_support.observed import (
    httpd_observed_for_id,
    httpd_observed_list_for_id,
    nghttpx_observed_for_id,
    nghttpx_observed_list_for_id,
    ws_observed_for_id,
)
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


def _fmt_args(template: List[str], ctx: Dict) -> List[str]:
    return [str(x).format(**ctx) for x in template]

def _append_req_id(url: str, req_id: str) -> str:
    sep = "&" if "?" in url else "?"
    return f"{url}{sep}id={req_id}"

def _ws_expected_pingpong() -> bytes:
    return b"x" * 125


def _ws_expected_data_small() -> bytes:
    pattern = b"0123456789"
    repeats = 2  # 对齐 cli_ws_data 默认 count=1 => 发送/接收 2 次
    out = bytearray()
    for length in range(1, 11):
        msg = bytes(pattern[i % len(pattern)] for i in range(length))
        for _ in range(repeats):
            out.extend(msg)
    return bytes(out)

def _download_files_for_baseline(env, client_name: str, count: int) -> List[Path]:
    run_dir = Path(env.gen_dir) / client_name
    return [run_dir / f"download_{i}.data" for i in range(count)]


def _download_files_for_qcurl(qcurl_artifact_path: Path, count: int) -> List[Path]:
    run_dir = qcurl_artifact_path.parent / "qcurl_run"
    return [run_dir / f"download_{i}.data" for i in range(count)]


def _response_items_from_files(files: List[Path], *, proto: str, status: int) -> List[Dict]:
    items: List[Dict] = []
    for f in files:
        body_len, body_sha256 = sha256_file(f)
        items.append({
            "status": int(status),
            "http_version": str(proto),
            "headers": {},
            "body_len": int(body_len),
            "body_sha256": str(body_sha256),
        })
    return items


def _build_request_proto(case_id: str, defaults: Dict) -> Dict:
    method = "GET"
    if case_id.startswith("upload_"):
        method = "POST" if "post" in case_id else "PUT"
    elif case_id.startswith("ws_"):
        method = "GET"  # WS 握手

    headers: Dict[str, str] = {}
    body = b""
    if case_id.startswith("upload_"):
        upload_size = int(defaults.get("upload_size", 0))
        body = b"x" * upload_size
        headers["Content-Length"] = str(upload_size)
    elif case_id == "download_range_resume":
        abort_offset = int(defaults.get("abort_offset", 0))
        file_size = int(defaults.get("file_size", 0))
        if abort_offset > 0 and file_size > abort_offset:
            headers["Range"] = f"bytes={abort_offset}-{file_size - 1}"
    return {
        "method": method,
        "url": defaults["url"],
        "headers": headers,
        "body": body,
    }


def _build_response_proto(case_id: str, defaults: Dict) -> Dict:
    status = 200
    http_version = defaults.get("proto", "h2")
    body = None
    if case_id.startswith("ws_"):
        status = 101
        http_version = "ws"
        if case_id == "ws_pingpong_small":
            body = _ws_expected_pingpong()
        elif case_id == "ws_data_small":
            body = _ws_expected_data_small()
    return {
        "status": status,
        "http_version": http_version,
        "headers": {},
        "body": body,
    }


@pytest.mark.parametrize("case_id", sorted(P0_CASES.keys()))
def test_p0_consistency(case_id, env, lc_logs, request, tmp_path):
    case = P0_CASES[case_id]
    is_ws_case = str(case_id).startswith("ws_")
    collect_logs = should_collect_service_logs()
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    require_http3 = (os.environ.get("QCURL_REQUIRE_HTTP3") or "").strip().lower()
    require_http3_enabled = require_http3 in ("1", "true", "yes", "on")
    if require_http3_enabled and not env.have_h3():
        pytest.fail(
            "QCURL_REQUIRE_HTTP3=1 但当前 testenv 无 HTTP/3 能力（env.have_h3()=False）。"
            "请先确保 nghttpx-h3 可用并启用 h3 testenv，或在非强制 job 中移除该变量。"
        )

    http_protos = ["http/1.1", "h2"]
    if env.have_h3():
        http_protos.append("h3")

    protos_to_run = [None] if is_ws_case else http_protos

    for proto in protos_to_run:
        ws_logs = {}
        if is_ws_case:
            # 仅在 ws_* case 中启动 ws echo server，避免无关用例对 ws_port / ws 能力的隐式依赖。
            ws_logs = request.getfixturevalue("lc_ws_logs")

        trace_base = f"lc_{uuid.uuid4().hex[:8]}_{case_id}"
        if proto is not None:
            trace_base = f"{trace_base}_{proto.replace('/', '_')}"
        baseline_req_id = f"{trace_base}__baseline"
        qcurl_req_id = f"{trace_base}__qcurl"

        resolved_defaults = dict(case["defaults"])
        if proto is not None:
            resolved_defaults["proto"] = proto
        if is_ws_case:
            resolved_defaults["url"] = str(resolved_defaults["url"]).format(ws_port=env.ws_port)
        else:
            resolved_defaults["url"] = str(resolved_defaults["url"]).format(https_port=env.https_port)
        resolved_defaults["url"] = _append_req_id(resolved_defaults["url"], baseline_req_id)

        args = _fmt_args(case["args_template"], resolved_defaults)
        req_meta = _build_request_proto(case_id, resolved_defaults)
        resp_meta = _build_response_proto(case_id, resolved_defaults)
        baseline_download_count = case.get("baseline_download_count")
        qcurl_download_count = case.get("qcurl_download_count")

        case_variant = case["case"]
        if proto is not None:
            case_variant = f"{case_variant}_{proto.replace('/', '_')}"

        case_env = {
            "QCURL_LC_CASE_ID": case_id,
            "QCURL_LC_PROTO": str(resolved_defaults.get("proto", "h2")),
            "QCURL_LC_COUNT": str(resolved_defaults.get("count", 1)),
            "QCURL_LC_DOCNAME": str(resolved_defaults.get("docname", "")),
            "QCURL_LC_UPLOAD_SIZE": str(resolved_defaults.get("upload_size", 0)),
            "QCURL_LC_ABORT_OFFSET": str(resolved_defaults.get("abort_offset", 0)),
            "QCURL_LC_FILE_SIZE": str(resolved_defaults.get("file_size", 0)),
            "QCURL_LC_REQ_ID": qcurl_req_id,
        }
        if is_ws_case:
            case_env["QCURL_LC_WS_PORT"] = str(env.ws_port)
        else:
            case_env["QCURL_LC_HTTPS_PORT"] = str(env.https_port)

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
                    download_count=baseline_download_count,
                )
            except FileNotFoundError as exc:
                pytest.skip(f"libtests 未构建: {exc}")

            # 将 request/response 的关键语义从“构造值”升级为“观测值”（服务端日志）
            if case_id.startswith("ws_"):
                ws_log = Path(ws_logs["ws_handshake_log"])
                obs = ws_observed_for_id(ws_log, baseline_req_id)
                baseline["payload"]["request"]["method"] = obs.method
                baseline["payload"]["request"]["url"] = obs.url
                baseline["payload"]["request"]["headers"] = obs.headers
            else:
                require_range = case_id == "download_range_resume"
                expected_proto = str(resolved_defaults.get("proto", "h2"))
                # upload_post_reuse：基线（cli_hx_upload）在 http/1.1 路径下可能走 chunked（无 Content-Length），
                # 而 QCurl（POSTFIELDS+SIZE）会显式带 Content-Length；两者在“请求体字节”层面可对齐，但头字段不可稳定对齐。
                include_content_length = case_id != "upload_post_reuse"
                if expected_proto == "h3":
                    access_log = Path(lc_logs["nghttpx_access_log"])
                    obs = nghttpx_observed_for_id(
                        access_log,
                        baseline_req_id,
                        require_range=require_range,
                        include_content_length=include_content_length,
                    )
                else:
                    access_log = Path(lc_logs["httpd_access_log"])
                    obs = httpd_observed_for_id(
                        access_log,
                        baseline_req_id,
                        require_range=require_range,
                        include_content_length=include_content_length,
                    )
                assert obs.http_version == expected_proto
                baseline["payload"]["request"]["method"] = obs.method
                baseline["payload"]["request"]["url"] = obs.url
                baseline["payload"]["request"]["headers"] = obs.headers
                baseline["payload"]["response"]["status"] = obs.status
                baseline["payload"]["response"]["http_version"] = obs.http_version

                expected_requests = int(resolved_defaults.get("count", 1))
                if case_id == "download_range_resume":
                    expected_requests = 2  # 首段（非 Range）+ 续传（Range）
                if expected_requests > 1:
                    if expected_proto == "h3":
                        obs_list = nghttpx_observed_list_for_id(
                            Path(lc_logs["nghttpx_access_log"]),
                            baseline_req_id,
                            expected_count=expected_requests,
                            include_content_length=include_content_length,
                        )
                    else:
                        obs_list = httpd_observed_list_for_id(
                            Path(lc_logs["httpd_access_log"]),
                            baseline_req_id,
                            expected_count=expected_requests,
                            include_content_length=include_content_length,
                        )
                    # 以 key 排序稳定化（避免并发/完成顺序差异造成假失败）
                    obs_list_sorted = sorted(
                        obs_list,
                        key=lambda o: (
                            o.url,
                            o.method,
                            str(sorted((o.headers or {}).items())),
                        ),
                    )
                    body = req_meta.get("body") or b""
                    import hashlib
                    body_sha256 = hashlib.sha256(body).hexdigest() if body else ""
                    baseline["payload"]["requests"] = [{
                        "method": o.method,
                        "url": o.url,
                        "headers": o.headers,
                        "body_len": int(len(body)),
                        "body_sha256": body_sha256,
                    } for o in obs_list_sorted]

                expected_responses = int(baseline_download_count or 0)
                if expected_responses > 1:
                    files = _download_files_for_baseline(env, case["client"], expected_responses)
                    missing = [str(p) for p in files if not p.exists()]
                    if missing:
                        raise AssertionError(f"baseline missing download files: {missing}")
                    baseline["payload"]["responses"] = _response_items_from_files(files, proto=expected_proto, status=obs.status)
            # 重写 baseline artifacts
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
                download_count=qcurl_download_count,
                case_env=case_env,
            )

            if case_id.startswith("ws_"):
                ws_log = Path(ws_logs["ws_handshake_log"])
                obs = ws_observed_for_id(ws_log, qcurl_req_id)
                qcurl["payload"]["request"]["method"] = obs.method
                qcurl["payload"]["request"]["url"] = obs.url
                qcurl["payload"]["request"]["headers"] = obs.headers
            else:
                require_range = case_id == "download_range_resume"
                expected_proto = str(resolved_defaults.get("proto", "h2"))
                include_content_length = case_id != "upload_post_reuse"
                if expected_proto == "h3":
                    access_log = Path(lc_logs["nghttpx_access_log"])
                    obs = nghttpx_observed_for_id(
                        access_log,
                        qcurl_req_id,
                        require_range=require_range,
                        include_content_length=include_content_length,
                    )
                else:
                    access_log = Path(lc_logs["httpd_access_log"])
                    obs = httpd_observed_for_id(
                        access_log,
                        qcurl_req_id,
                        require_range=require_range,
                        include_content_length=include_content_length,
                    )
                assert obs.http_version == expected_proto
                qcurl["payload"]["request"]["method"] = obs.method
                qcurl["payload"]["request"]["url"] = obs.url
                qcurl["payload"]["request"]["headers"] = obs.headers
                qcurl["payload"]["response"]["status"] = obs.status
                qcurl["payload"]["response"]["http_version"] = obs.http_version

                expected_requests = int(resolved_defaults.get("count", 1))
                if case_id == "download_range_resume":
                    expected_requests = 2
                if expected_requests > 1:
                    if expected_proto == "h3":
                        obs_list = nghttpx_observed_list_for_id(
                            Path(lc_logs["nghttpx_access_log"]),
                            qcurl_req_id,
                            expected_count=expected_requests,
                            include_content_length=include_content_length,
                        )
                    else:
                        obs_list = httpd_observed_list_for_id(
                            Path(lc_logs["httpd_access_log"]),
                            qcurl_req_id,
                            expected_count=expected_requests,
                            include_content_length=include_content_length,
                        )
                    obs_list_sorted = sorted(
                        obs_list,
                        key=lambda o: (
                            o.url,
                            o.method,
                            str(sorted((o.headers or {}).items())),
                        ),
                    )
                    body = req_meta.get("body") or b""
                    import hashlib
                    body_sha256 = hashlib.sha256(body).hexdigest() if body else ""
                    qcurl["payload"]["requests"] = [{
                        "method": o.method,
                        "url": o.url,
                        "headers": o.headers,
                        "body_len": int(len(body)),
                        "body_sha256": body_sha256,
                    } for o in obs_list_sorted]

                expected_responses = int(qcurl_download_count or 0)
                if expected_responses > 1:
                    files = _download_files_for_qcurl(Path(qcurl["path"]), expected_responses)
                    missing = [str(p) for p in files if not p.exists()]
                    if missing:
                        raise AssertionError(f"qcurl missing download files: {missing}")
                    qcurl["payload"]["responses"] = _response_items_from_files(files, proto=expected_proto, status=obs.status)
            write_json(qcurl["path"], qcurl["payload"])

            assert_artifacts_match(baseline["path"], qcurl["path"])
        except Exception:
            if collect_logs:
                collect_service_logs_for_case(
                    env,
                    suite=case["suite"],
                    case=case_variant,
                    logs={**lc_logs, **ws_logs},
                    meta={
                        "case_id": case_id,
                        "case_variant": case_variant,
                        "proto": str(proto or "ws"),
                        "baseline_req_id": baseline_req_id,
                        "qcurl_req_id": qcurl_req_id,
                    },
                )
            raise
