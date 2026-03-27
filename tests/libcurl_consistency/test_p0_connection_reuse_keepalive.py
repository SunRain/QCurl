"""
P0：HTTP/1.1 keep-alive 连接复用一致性。

从服务端观测日志比较 `unique_connections` 与归一化 `conn_seq`，
不比较内部连接池实现。
"""

from __future__ import annotations

import json
import os
import uuid
from pathlib import Path
from typing import Dict, List
from urllib.parse import parse_qs, urlencode, urlsplit, urlunsplit

import pytest

from tests.libcurl_consistency.pytest_support.artifacts import build_request_semantic, write_json
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs


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


def _connection_boundary(proto: str) -> Dict:
    return {
        "scheme": "http",
        "http_version": proto,
        "proxy_mode": "direct",
        "alpn": proto,
        "sni": False,
        "client_cert": False,
        "pinning": "none",
    }


def _boundary_key(boundary: Dict) -> str:
    return ";".join(f"{key}={boundary[key]}" for key in sorted(boundary))


def _make_ctbp_payload(*, proto: str, case_variant: str, observed: Dict) -> Dict:
    boundary = _connection_boundary(proto)
    boundary_key = _boundary_key(boundary)
    conn_seq = [int(item) for item in observed.get("conn_seq") or []]
    return {
        "schema": "qcurl-lc/ctbp@v1",
        "kind": "connection_reuse",
        "boundary": boundary,
        "boundary_key": boundary_key,
        "connection_group_id": f"{case_variant}:{proto}",
        "request_count": int(observed.get("request_count") or 0),
        "unique_connections": int(observed.get("unique_connections") or 0),
        "expected_unique_connections": 1,
        "conn_seq": conn_seq,
        "requests": [
            {
                "seq": index + 1,
                "conn_id": conn_id,
                "boundary_key": boundary_key,
            }
            for index, conn_id in enumerate(conn_seq)
        ],
    }


def test_p0_connection_reuse_keepalive_http_1_1(env, lc_logs, lc_observe_http, tmp_path):
    qt_bin = os.environ.get("QCURL_QTTEST")
    qt_path = Path(qt_bin).resolve() if qt_bin else None
    if not qt_path or not qt_path.exists():
        pytest.skip("当前环境未提供 QCURL_QTTEST 可执行文件，跳过该用例")

    collect_logs = should_collect_service_logs()
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    suite = "p0_conn"
    proto = "http/1.1"
    case_variant = "p0_connection_reuse_keepalive_http_1.1"
    repeat = 5

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_p0_keepalive_reuse"
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
        baseline["payload"]["ctbp"] = _make_ctbp_payload(
            proto=proto,
            case_variant=case_variant,
            observed=baseline["payload"]["connection_observed"],
        )
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
        qcurl["payload"]["ctbp"] = _make_ctbp_payload(
            proto=proto,
            case_variant=case_variant,
            observed=qcurl["payload"]["connection_observed"],
        )
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
                logs={**lc_logs, "observe_http_log": observe_log},
                meta={
                    "case_id": "p0_connection_reuse_keepalive",
                    "proto": proto,
                    "observe_http_port": str(port),
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "repeat": str(repeat),
                },
            )
        raise
