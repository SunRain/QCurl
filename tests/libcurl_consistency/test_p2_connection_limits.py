"""
P2：连接上限合同。

通过 baseline/QCurl 双侧的连接摘要，证明连接上限真正生效，而不是只验证“不崩溃”。
"""

from __future__ import annotations

import json
import os
import uuid
from pathlib import Path
from typing import Dict, List
from urllib.parse import parse_qs, urlencode, urlsplit, urlunsplit

import pytest

from tests.libcurl_consistency.pytest_support.compare import assert_artifacts_match
from tests.libcurl_consistency.pytest_support.baseline import run_libtest_case
from tests.libcurl_consistency.pytest_support.qcurl_runner import run_qt_test
from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs_for_case, should_collect_service_logs
from tests.libcurl_consistency.pytest_support.artifacts import write_json


if os.environ.get("QCURL_LC_EXT", "").strip() != "1":
    pytest.skip("该扩展用例仅在 QCURL_LC_EXT=1 时启用", allow_module_level=True)


def _strip_query_id(url: str) -> str:
    parts = urlsplit(url)
    query = parse_qs(parts.query, keep_blank_values=True)
    query.pop("id", None)
    return urlunsplit(("", "", parts.path, urlencode(query, doseq=True), ""))


def _load_json(path: Path) -> Dict:
    return json.loads(path.read_text(encoding="utf-8"))


def _load_jsonl(path: Path) -> List[Dict]:
    if not path.exists():
        return []
    entries: List[Dict] = []
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not raw.strip():
            continue
        try:
            entries.append(json.loads(raw))
        except json.JSONDecodeError:
            continue
    return entries


def _entries_for_id(observe_log: Path, req_id: str) -> List[Dict]:
    return [entry for entry in _load_jsonl(observe_log) if str(entry.get("id") or "") == req_id]


def _conn_seq_from_ports(local_ports: List[int]) -> List[int]:
    mapping: Dict[int, int] = {}
    conn_seq: List[int] = []
    next_id = 1
    for port in local_ports:
        if port not in mapping:
            mapping[port] = next_id
            next_id += 1
        conn_seq.append(mapping[port])
    return conn_seq


def _baseline_connection_observed(summary: Dict) -> Dict:
    local_ports = [int(item) for item in summary.get("local_ports") or []]
    conn_seq = _conn_seq_from_ports(local_ports)
    return {
        "request_count": int(summary.get("request_count") or len(local_ports)),
        "unique_connections": int(summary.get("unique_connections") or max(conn_seq, default=0)),
        "conn_seq": conn_seq,
    }


def _qcurl_connection_observed(summary: Dict, *, expected_count: int) -> Dict:
    request_count = int(summary.get("request_count") or 0)
    unique_connections = int(summary.get("unique_connections") or 0)
    if request_count != expected_count:
        raise AssertionError(
            f"QCurl connection summary request_count 不匹配: got={request_count}, expected={expected_count}"
        )
    conn_seq = [1] * request_count if unique_connections == 1 else []
    return {
        "request_count": request_count,
        "unique_connections": unique_connections,
        "conn_seq": conn_seq,
    }


def _observed_connection(entries: List[Dict]) -> Dict:
    ports: List[int] = []
    for entry in entries:
        ports.append(int(entry.get("peer_port") or 0))
    conn_seq = _conn_seq_from_ports(ports)
    return {
        "request_count": len(entries),
        "unique_connections": max(conn_seq, default=0),
        "conn_seq": conn_seq,
    }


def _strip_all_query(url: str) -> str:
    parts = urlsplit(url)
    return urlunsplit(("", "", parts.path, "", ""))


def test_p2_connection_limits_contract(env, lc_logs, lc_observe_http, tmp_path):
    qt_path = Path(os.environ["QCURL_QTTEST"])
    collect_logs = should_collect_service_logs()

    suite = "p2_connection_limits"
    case = "lc_p2_connection_limits_contract_http_1_1"
    proto = "http/1.1"
    repeat = 4

    trace_base = f"lc_{uuid.uuid4().hex[:8]}_conn_limits"
    baseline_req_id = f"{trace_base}__baseline"
    qcurl_req_id = f"{trace_base}__qcurl"

    qcurl_summary = tmp_path / "qcurl_connection_summary.json"
    port = int(lc_observe_http["port"])
    observe_log = Path(str(lc_observe_http["log_file"]))

    baseline_url = f"http://localhost:{port}/empty_200?id={baseline_req_id}"
    qcurl_url = f"http://localhost:{port}/empty_200?slot=0001&id={qcurl_req_id}"

    response_meta = {"status": 200, "http_version": proto, "headers": {}, "body": b""}

    try:
        observe_log.write_text("", encoding="utf-8")
        baseline = run_libtest_case(
            env=env,
            suite=suite,
            case=case,
            client_name="cli_lc_http",
            args=[
                "-V",
                proto,
                "--repeat",
                str(repeat),
                baseline_url,
            ],
            request_meta={
                "method": "GET",
                "url": baseline_url,
                "headers": {},
                "body": b"",
            },
            response_meta=response_meta,
            download_count=None,
        )

        baseline_entries = _entries_for_id(observe_log, baseline_req_id)
        if len(baseline_entries) != repeat:
            raise AssertionError(
                f"observe http log 记录数不匹配（baseline）: got={len(baseline_entries)}, expected={repeat}"
            )
        baseline_conn = _observed_connection(baseline_entries)
        baseline["payload"]["request"]["url"] = _strip_all_query(str(baseline["payload"]["request"]["url"] or ""))
        baseline["payload"]["connection_observed"] = baseline_conn
        write_json(baseline["path"], baseline["payload"])

        observe_log.write_text("", encoding="utf-8")
        qcurl = run_qt_test(
            env=env,
            suite=suite,
            case=case,
            qt_executable=qt_path,
            args=[],
            request_meta={"method": "GET", "url": qcurl_url, "headers": {}, "body": b""},
            response_meta=response_meta,
            download_count=repeat,
            case_env={
                "QCURL_LC_CASE_ID": "multi_limits_smoke",
                "QCURL_LC_PROTO": proto,
                "QCURL_LC_OBSERVE_HTTP_PORT": str(port),
                "QCURL_LC_COUNT": str(repeat),
                "QCURL_LC_UPLOAD_SIZE": "0",
                "QCURL_LC_ABORT_OFFSET": "0",
                "QCURL_LC_FILE_SIZE": "0",
                "QCURL_LC_REQ_ID": qcurl_req_id,
                "QCURL_LC_CONN_SUMMARY_PATH": str(qcurl_summary),
            },
        )

        qcurl_entries = _entries_for_id(observe_log, qcurl_req_id)
        if len(qcurl_entries) != repeat:
            raise AssertionError(
                f"observe http log 记录数不匹配（qcurl）: got={len(qcurl_entries)}, expected={repeat}"
            )
        qcurl_conn = _observed_connection(qcurl_entries)
        qcurl_internal_conn = _qcurl_connection_observed(_load_json(qcurl_summary), expected_count=repeat)
        qcurl["payload"]["request"]["url"] = _strip_all_query(str(qcurl["payload"]["request"]["url"] or ""))
        qcurl["payload"]["connection_observed"] = qcurl_conn
        write_json(qcurl["path"], qcurl["payload"])

        assert baseline_conn["request_count"] == repeat
        assert qcurl_conn["request_count"] == repeat
        assert baseline_conn["unique_connections"] == 1
        assert qcurl_conn["unique_connections"] == 1
        assert qcurl_internal_conn["unique_connections"] == 1
        assert_artifacts_match(baseline["path"], qcurl["path"])
    except Exception:
        if collect_logs:
            collect_service_logs_for_case(
                env,
                suite=suite,
                case=case,
                logs={**lc_logs, "observe_http_log": observe_log},
                meta={
                    "case_id": "p2_connection_limits_contract",
                    "proto": proto,
                    "repeat": str(repeat),
                    "observe_http_port": str(port),
                    "baseline_req_id": baseline_req_id,
                    "qcurl_req_id": qcurl_req_id,
                    "qcurl_summary": str(qcurl_summary),
                },
            )
        raise
