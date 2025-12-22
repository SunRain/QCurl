"""
可选扩展套件（LC-11）：
- 默认不作为 Gate，避免波动。
- 显式开启：QCURL_LC_EXT=1

当前扩展覆盖：
- 并发下载压力（h2/h3）：用于覆盖 multiplexing/调度路径与数据一致性。
"""

from __future__ import annotations

import json
import os
import uuid
from pathlib import Path
from typing import Dict, List
from urllib.parse import parse_qs, urlencode, urlsplit, urlunsplit

import pytest

from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.case_defs import EXT_CASES
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.artifacts import build_request_semantic, sha256_file, write_json
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

def _strip_query_id(path_or_url: str) -> str:
    parts = urlsplit(path_or_url)
    q = parse_qs(parts.query, keep_blank_values=True)
    q.pop("id", None)
    query = urlencode(q, doseq=True)
    return urlunsplit(("", "", parts.path, query, ""))

def _load_jsonl(path: Path) -> List[Dict]:
    if not path.exists():
        return []
    out: List[Dict] = []
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not raw.strip():
            continue
        try:
            out.append(json.loads(raw))
        except json.JSONDecodeError:
            continue
    return out

def _entries_for_id(observe_log: Path, req_id: str) -> List[Dict]:
    return [e for e in _load_jsonl(observe_log) if (e.get("id") or "") == req_id]

def _conn_seq_from_ports(peer_ports: List[int]) -> List[int]:
    mapping: Dict[int, int] = {}
    seq: List[int] = []
    next_id = 1
    for p in peer_ports:
        if p not in mapping:
            mapping[p] = next_id
            next_id += 1
        seq.append(mapping[p])
    return seq

def _connection_observed(entries: List[Dict]) -> Dict:
    ports: List[int] = []
    for e in entries:
        try:
            ports.append(int(e.get("peer_port") or 0))
        except Exception:
            ports.append(0)
    seq = _conn_seq_from_ports(ports)
    unique = max(seq) if seq else 0
    return {
        "request_count": len(entries),
        "unique_connections": unique,
        "conn_seq": seq,
    }

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


def test_ext_reuse_keepalive_http_1_1(env, lc_logs, lc_observe_http, tmp_path):
    """
    LC-31：连接复用可观测一致性（HTTP/1.1 keep-alive）。
    - 通过观测服务端日志中的 peer_port 统计单个 run 内的连接数
    - 仅比较可比统计：unique_connections + 归一化 conn_seq
    """
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("QCURL_QTTEST 未设置或可执行不存在")

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "ext"
    proto = "http/1.1"
    case_variant = "lc_ext_reuse_keepalive_http_1.1"
    repeat = 5

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_reuse_keepalive"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    base_url = f"http://localhost:{port}/empty_200"
    baseline_url = _append_req_id(base_url, baseline_req_id)
    qcurl_url = _append_req_id(base_url, qcurl_req_id)
    resp_meta = {"status": 200, "http_version": proto, "headers": {}, "body": None}

    try:
        observe_log.write_text("", encoding="utf-8")
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case_variant,
            client_name="cli_lc_http",
            args=[
                "-V",
                proto,
                "--repeat",
                str(repeat),
                baseline_url,
            ],
            request_meta={"method": "GET", "url": baseline_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
        )

        b_entries = _entries_for_id(observe_log, baseline_req_id)
        if len(b_entries) != repeat:
            raise AssertionError(f"observe http log 记录数不匹配：got={len(b_entries)}, expected={repeat}")
        b0 = b_entries[0]
        baseline["payload"]["request"] = build_request_semantic(
            str(b0.get("method") or "GET"),
            _strip_query_id(str(b0.get("path") or "")),
            b0.get("headers") or {},
            b"",
        )
        baseline["payload"]["response"]["status"] = int(b0.get("status") or 0)
        baseline["payload"]["response"]["http_version"] = proto
        baseline["payload"]["response"]["headers"] = b0.get("response_headers") or {}
        baseline["payload"]["connection_observed"] = _connection_observed(b_entries)
        write_json(baseline["path"], baseline["payload"])

        observe_log.write_text("", encoding="utf-8")
        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case_variant,
            qt_executable=qt_path,
            args=[],
            request_meta={"method": "GET", "url": qcurl_url, "headers": {}, "body": b""},
            response_meta=resp_meta,
            download_count=1,
            case_env={
                "QCURL_LC_CASE_ID": "ext_reuse_keepalive",
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_OBSERVE_HTTP_PORT": str(port),
                "QCURL_LC_COUNT": str(repeat),
            },
        )

        q_entries = _entries_for_id(observe_log, qcurl_req_id)
        if len(q_entries) != repeat:
            raise AssertionError(f"observe http log 记录数不匹配：got={len(q_entries)}, expected={repeat}")
        q0 = q_entries[0]
        qcurl["payload"]["request"] = build_request_semantic(
            str(q0.get("method") or "GET"),
            _strip_query_id(str(q0.get("path") or "")),
            q0.get("headers") or {},
            b"",
        )
        qcurl["payload"]["response"]["status"] = int(q0.get("status") or 0)
        qcurl["payload"]["response"]["http_version"] = proto
        qcurl["payload"]["response"]["headers"] = q0.get("response_headers") or {}
        qcurl["payload"]["connection_observed"] = _connection_observed(q_entries)
        write_json(qcurl["path"], qcurl["payload"])

        assert baseline["payload"]["connection_observed"]["unique_connections"] == 1
        assert qcurl["payload"]["connection_observed"]["unique_connections"] == 1

        assert_artifacts_match(baseline["path"], qcurl["path"])
    except Exception:
        if collect_logs:
            collect_service_logs_for_case(
                env,
                suite=suite,
                case=case_variant,
                logs=lc_logs,
                meta={
                    "case_id": "ext_reuse_keepalive",
                    "proto": proto,
                    "observe_http_port": port,
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "repeat": repeat,
                },
            )
        raise
