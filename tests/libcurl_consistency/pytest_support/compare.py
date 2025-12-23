"""
artifacts 对比器（LC-5）：
- 按 LC-0 强制字段比较：request 语义摘要、response 长度/sha256/status/http_version。
- 可返回 diff 列表供 pytest 断言或报告输出。
"""

from __future__ import annotations

from pathlib import Path
from typing import Dict, List, Tuple
import json


def _load(path: Path) -> Dict:
    return json.loads(path.read_text(encoding="utf-8"))

def _find_event(events: List[Dict], event_type: str) -> Dict:
    for e in events or []:
        if e.get("type") == event_type:
            return e
    return {}

def _index_event_type(events: List[Dict], event_type: str) -> int:
    for idx, e in enumerate(events or []):
        if e.get("type") == event_type:
            return idx
    return -1

def _validate_pause_resume_contract(pr: Dict, *, side: str, body_len: int) -> List[str]:
    diffs: List[str] = []
    events = pr.get("events")
    if not isinstance(events, list) or not events:
        diffs.append(f"{side} pause_resume_strict.events missing or empty")
        return diffs

    last_seq = 0
    last_t = -1
    last_delivered = -1
    last_written = -1
    type_counts: Dict[str, int] = {}

    for idx, e in enumerate(events):
        if not isinstance(e, dict):
            diffs.append(f"{side} events[{idx}] not an object")
            continue

        seq = e.get("seq")
        t_us = e.get("t_us")
        delivered = e.get("bytes_delivered_total")
        written = e.get("bytes_written_total")
        etype = e.get("type")

        if not isinstance(seq, int) or seq <= last_seq:
            diffs.append(f"{side} event seq not increasing at idx={idx}: {seq} <= {last_seq}")
        else:
            last_seq = seq

        if not isinstance(t_us, int) or t_us < 0:
            diffs.append(f"{side} event t_us invalid at idx={idx}: {t_us}")
        elif t_us < last_t:
            diffs.append(f"{side} event t_us not monotonic at idx={idx}: {t_us} < {last_t}")
        else:
            last_t = t_us

        if not isinstance(delivered, int) or delivered < 0:
            diffs.append(f"{side} event bytes_delivered_total invalid at idx={idx}: {delivered}")
        elif delivered < last_delivered:
            diffs.append(f"{side} bytes_delivered_total not monotonic at idx={idx}: {delivered} < {last_delivered}")
        else:
            last_delivered = delivered

        if not isinstance(written, int) or written < 0:
            diffs.append(f"{side} event bytes_written_total invalid at idx={idx}: {written}")
        elif written < last_written:
            diffs.append(f"{side} bytes_written_total not monotonic at idx={idx}: {written} < {last_written}")
        else:
            last_written = written

        if not isinstance(etype, str) or not etype:
            diffs.append(f"{side} event type invalid at idx={idx}: {etype}")
        else:
            type_counts[etype] = type_counts.get(etype, 0) + 1

    required_once = ["start", "pause_req", "pause_effective", "resume_req", "finished"]
    for t in required_once:
        if type_counts.get(t, 0) != 1:
            diffs.append(f"{side} event {t} count mismatch: {type_counts.get(t, 0)} != 1")

    pr_idx = _index_event_type(events, "pause_req")
    pe_idx = _index_event_type(events, "pause_effective")
    rr_idx = _index_event_type(events, "resume_req")
    fin_idx = _index_event_type(events, "finished")
    if pr_idx >= 0 and pe_idx >= 0 and pe_idx <= pr_idx:
        diffs.append(f"{side} event order invalid: pause_effective idx={pe_idx} <= pause_req idx={pr_idx}")
    if pe_idx >= 0 and rr_idx >= 0 and rr_idx <= pe_idx:
        diffs.append(f"{side} event order invalid: resume_req idx={rr_idx} <= pause_effective idx={pe_idx}")
    if rr_idx >= 0 and fin_idx >= 0 and fin_idx <= rr_idx:
        diffs.append(f"{side} event order invalid: finished idx={fin_idx} <= resume_req idx={rr_idx}")

    pe = _find_event(events, "pause_effective")
    rr = _find_event(events, "resume_req")
    if pe and rr:
        for f in ("bytes_delivered_total", "bytes_written_total"):
            pe_v = pe.get(f)
            rr_v = rr.get(f)
            if isinstance(pe_v, int) and isinstance(rr_v, int) and rr_v != pe_v:
                diffs.append(f"{side} pause window {f} delta != 0: {pe_v} -> {rr_v}")

    fin = _find_event(events, "finished")
    if fin:
        fin_written = fin.get("bytes_written_total")
        if isinstance(fin_written, int) and body_len >= 0 and fin_written != body_len:
            diffs.append(f"{side} finished bytes_written_total mismatch: {fin_written} != body_len {body_len}")

    return diffs


def _cmp_dict(lhs: Dict, rhs: Dict, fields: List[str]) -> List[str]:
    diffs = []
    for f in fields:
        if lhs.get(f) != rhs.get(f):
            diffs.append(f"{f} mismatch: {lhs.get(f)} != {rhs.get(f)}")
    return diffs


def _cmp_list_dict(lhs_list: List[Dict], rhs_list: List[Dict], fields: List[str], label: str) -> List[str]:
    diffs: List[str] = []
    if len(lhs_list) != len(rhs_list):
        diffs.append(f"{label} length mismatch: {len(lhs_list)} != {len(rhs_list)}")
        return diffs
    for idx, (lhs, rhs) in enumerate(zip(lhs_list, rhs_list)):
        for f in fields:
            if lhs.get(f) != rhs.get(f):
                diffs.append(f"{label}[{idx}].{f} mismatch: {lhs.get(f)} != {rhs.get(f)}")
    return diffs


def compare_artifacts(baseline_path: Path, qcurl_path: Path) -> Tuple[bool, List[str]]:
    """
    比较 baseline 与 QCurl artifacts。返回 (是否一致, 差异列表)。
    - 仅比较双方都存在的字段；缺失视为差异。
    """
    base = _load(baseline_path)
    qc = _load(qcurl_path)
    diffs: List[str] = []

    # 请求语义摘要（P0 必做）
    b_reqs = base.get("requests")
    q_reqs = qc.get("requests")
    if b_reqs is not None or q_reqs is not None:
        if not b_reqs or not q_reqs:
            diffs.append("requests missing in one side")
        else:
            diffs.extend(_cmp_list_dict(
                b_reqs,
                q_reqs,
                ["method", "url", "headers", "body_len", "body_sha256"],
                "requests",
            ))
    else:
        b_req = base.get("request")
        q_req = qc.get("request")
        if not b_req or not q_req:
            diffs.append("request missing in one side")
        else:
            diffs.extend(_cmp_dict(b_req, q_req, ["method", "url", "headers", "body_len", "body_sha256"]))

    # 响应摘要（或下载文件摘要）
    b_resps = base.get("responses")
    q_resps = qc.get("responses")
    if b_resps is not None or q_resps is not None:
        if not b_resps or not q_resps:
            diffs.append("responses missing in one side")
        else:
            diffs.extend(_cmp_list_dict(
                b_resps,
                q_resps,
                ["status", "http_version", "headers", "body_len", "body_sha256"],
                "responses",
            ))

    b_resp = base.get("response")
    q_resp = qc.get("response")
    if not b_resp or not q_resp:
        diffs.append("response missing in one side")
    else:
        diffs.extend(_cmp_dict(b_resp, q_resp, ["status", "http_version", "headers", "body_len", "body_sha256"]))

        # 可选：原始响应头观测（LC-26）
        for opt in ("headers_raw_len", "headers_raw_sha256", "headers_raw_lines"):
            b_has = opt in b_resp
            q_has = opt in q_resp
            if b_has != q_has:
                diffs.append(f"response.{opt} missing in one side")
                continue
            if b_has and q_has and b_resp.get(opt) != q_resp.get(opt):
                diffs.append(f"response.{opt} mismatch: {b_resp.get(opt)} != {q_resp.get(opt)}")

    # 可选：cookie jar 输出一致性（P1）
    b_cookiejar = base.get("cookiejar")
    q_cookiejar = qc.get("cookiejar")
    if b_cookiejar is not None or q_cookiejar is not None:
        if not b_cookiejar or not q_cookiejar:
            diffs.append("cookiejar missing in one side")
        else:
            diffs.extend(_cmp_dict(b_cookiejar, q_cookiejar, ["records", "sha256"]))

    # 可选：错误归一化输出一致性（P2）
    b_err = base.get("error")
    q_err = qc.get("error")
    if b_err is not None or q_err is not None:
        if b_err is None or q_err is None:
            diffs.append("error missing in one side")
        else:
            fields = ["kind", "http_status"]
            for opt in ("curlcode", "http_code"):
                b_has = opt in b_err
                q_has = opt in q_err
                if b_has != q_has:
                    diffs.append(f"error.{opt} missing in one side")
                    continue
                if b_has and q_has:
                    fields.append(opt)
            diffs.extend(_cmp_dict(b_err, q_err, fields))

    # 可选：进度摘要一致性（LC-30）
    b_prog = base.get("progress_summary")
    q_prog = qc.get("progress_summary")
    if b_prog is not None or q_prog is not None:
        if b_prog is None or q_prog is None:
            diffs.append("progress_summary missing in one side")
        else:
            for lane in ("download", "upload"):
                b_lane = b_prog.get(lane)
                q_lane = q_prog.get(lane)
                if b_lane is None and q_lane is None:
                    continue
                if b_lane is None or q_lane is None:
                    diffs.append(f"progress_summary.{lane} missing in one side")
                    continue
                diffs.extend(_cmp_dict(b_lane, q_lane, ["monotonic", "now_max", "total_max"]))

    # 可选：连接复用/多路复用的可观测一致性（LC-31）
    b_conn = base.get("connection_observed")
    q_conn = qc.get("connection_observed")
    if b_conn is not None or q_conn is not None:
        if b_conn is None or q_conn is None:
            diffs.append("connection_observed missing in one side")
        else:
            diffs.extend(_cmp_dict(b_conn, q_conn, ["request_count", "unique_connections", "conn_seq"]))

    # 可选：pause/resume 弱判据一致性（LC-15a）
    # 说明：`cli_hx_download -P` 的 stderr 打点顺序不足以稳定定义“pause window”，
    # 因此这里只比较“事件存在性/顺序”（pause/resume/finished）与 offset/count，不比较 pause 期间数据事件。
    b_pr = base.get("pause_resume")
    q_pr = qc.get("pause_resume")
    if b_pr is not None or q_pr is not None:
        if b_pr is None or q_pr is None:
            diffs.append("pause_resume missing in one side")
        else:
            diffs.extend(_cmp_dict(
                b_pr,
                q_pr,
                ["pause_offset", "pause_count", "resume_count", "event_seq"],
            ))

    # 可选：pause/resume 强判据一致性（LC-15b：语义合同测试）
    # 核心：PauseEffective 之后到 ResumeReq 之前，bytes_delivered_total / bytes_written_total 必须严格不变（Δ=0）。
    b_prs = base.get("pause_resume_strict")
    q_prs = qc.get("pause_resume_strict")
    if b_prs is not None or q_prs is not None:
        if b_prs is None or q_prs is None:
            diffs.append("pause_resume_strict missing in one side")
        else:
            diffs.extend(_cmp_dict(
                b_prs,
                q_prs,
                ["schema", "proto", "pause_offset", "resume_delay_ms"],
            ))
            b_len = int((b_resp or {}).get("body_len") or 0)
            q_len = int((q_resp or {}).get("body_len") or 0)
            diffs.extend(_validate_pause_resume_contract(b_prs, side="baseline", body_len=b_len))
            diffs.extend(_validate_pause_resume_contract(q_prs, side="qcurl", body_len=q_len))

    return (len(diffs) == 0, diffs)


def assert_artifacts_match(baseline_path: Path, qcurl_path: Path) -> None:
    """pytest 断言辅助：不一致时抛出 AssertionError，并列出差异。"""
    ok, diffs = compare_artifacts(baseline_path, qcurl_path)
    if not ok:
        detail = "\n".join(diffs)
        raise AssertionError(f"Artifacts mismatch:\n{detail}")
