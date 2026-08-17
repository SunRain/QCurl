"""pause/resume 与 backpressure 流控工件合同。"""

from __future__ import annotations

from typing import Dict, List

from .compare_shared import compare_dict


def _find_event(events: List[Dict], event_type: str) -> Dict:
    for event in events or []:
        if event.get("type") == event_type:
            return event
    return {}


def _index_event_type(events: List[Dict], event_type: str) -> int:
    for index, event in enumerate(events or []):
        if event.get("type") == event_type:
            return index
    return -1


def _validate_event_counters(events: List[Dict], *, side: str) -> tuple[List[str], Dict[str, int]]:
    diffs: List[str] = []
    type_counts: Dict[str, int] = {}
    previous = {"seq": 0, "t_us": -1, "bytes_delivered_total": -1, "bytes_written_total": -1}
    for index, event in enumerate(events):
        if not isinstance(event, dict):
            diffs.append(f"{side} events[{index}] not an object")
            continue
        for field in previous:
            value = event.get(field)
            minimum = 1 if field == "seq" else 0
            if not isinstance(value, int) or value < minimum:
                diffs.append(f"{side} event {field} invalid at idx={index}: {value}")
                continue
            if value <= previous[field] and field == "seq":
                diffs.append(
                    f"{side} event seq not increasing at idx={index}: "
                    f"{value} <= {previous[field]}"
                )
            elif value < previous[field]:
                diffs.append(
                    f"{side} {field} not monotonic at idx={index}: "
                    f"{value} < {previous[field]}"
                )
            else:
                previous[field] = value
        event_type = event.get("type")
        if not isinstance(event_type, str) or not event_type:
            diffs.append(f"{side} event type invalid at idx={index}: {event_type}")
        else:
            type_counts[event_type] = type_counts.get(event_type, 0) + 1
    return diffs, type_counts


def _validate_pause_order(events: List[Dict], type_counts: Dict[str, int], *, side: str) -> List[str]:
    diffs: List[str] = []
    for event_type in ("start", "pause_req", "pause_effective", "resume_req", "finished"):
        if type_counts.get(event_type, 0) != 1:
            diffs.append(
                f"{side} event {event_type} count mismatch: "
                f"{type_counts.get(event_type, 0)} != 1"
            )
    indexes = {
        event_type: _index_event_type(events, event_type)
        for event_type in ("pause_req", "pause_effective", "resume_req", "finished")
    }
    ordered_pairs = (
        ("pause_req", "pause_effective"),
        ("pause_effective", "resume_req"),
        ("resume_req", "finished"),
    )
    for earlier, later in ordered_pairs:
        if indexes[earlier] >= 0 and indexes[later] >= 0 and indexes[later] <= indexes[earlier]:
            diffs.append(
                f"{side} event order invalid: {later} idx={indexes[later]} "
                f"<= {earlier} idx={indexes[earlier]}"
            )
    return diffs


def _validate_pause_window(events: List[Dict], *, side: str, body_len: int) -> List[str]:
    diffs: List[str] = []
    pause_effective = _find_event(events, "pause_effective")
    resume_request = _find_event(events, "resume_req")
    if pause_effective and resume_request:
        for field in ("bytes_delivered_total", "bytes_written_total"):
            paused_value = pause_effective.get(field)
            resumed_value = resume_request.get(field)
            if isinstance(paused_value, int) and isinstance(resumed_value, int):
                if resumed_value != paused_value:
                    diffs.append(
                        f"{side} pause window {field} delta != 0: "
                        f"{paused_value} -> {resumed_value}"
                    )
    finished = _find_event(events, "finished")
    if finished:
        written = finished.get("bytes_written_total")
        if isinstance(written, int) and body_len >= 0 and written != body_len:
            diffs.append(
                f"{side} finished bytes_written_total mismatch: "
                f"{written} != body_len {body_len}"
            )
    return diffs


def _validate_pause_resume_contract(contract: Dict, *, side: str, body_len: int) -> List[str]:
    events = contract.get("events")
    if not isinstance(events, list) or not events:
        return [f"{side} pause_resume_strict.events missing or empty"]
    diffs, type_counts = _validate_event_counters(events, side=side)
    diffs.extend(_validate_pause_order(events, type_counts, side=side))
    diffs.extend(_validate_pause_window(events, side=side, body_len=body_len))
    return diffs


def _validate_backpressure_contract(contract: Dict, *, side: str) -> List[str]:
    diffs: List[str] = []
    if contract.get("schema") != "qcurl-lc/backpressure@v1":
        return [f"{side} backpressure_contract.schema mismatch: {contract.get('schema')!r}"]
    limit_bytes = contract.get("limit_bytes")
    resume_bytes = contract.get("resume_bytes")
    max_write_size = contract.get("curl_max_write_size")
    if not isinstance(limit_bytes, int) or limit_bytes <= 0:
        return [f"{side} backpressure_contract.limit_bytes invalid: {limit_bytes!r}"]
    if not isinstance(resume_bytes, int) or resume_bytes <= 0 or resume_bytes >= limit_bytes:
        diffs.append(f"{side} backpressure_contract.resume_bytes invalid: {resume_bytes!r}")
    if not isinstance(max_write_size, int) or max_write_size <= 0:
        diffs.append(f"{side} backpressure_contract.curl_max_write_size invalid: {max_write_size!r}")
    if contract.get("event_seq") != ["bp_on", "bp_off"]:
        diffs.append(f"{side} backpressure_contract.event_seq invalid: {contract.get('event_seq')!r}")
    peak = contract.get("peak_buffered_bytes")
    if isinstance(peak, int) and peak < 0:
        diffs.append(f"{side} backpressure_contract.peak_buffered_bytes invalid: {peak!r}")
    if isinstance(peak, int) and isinstance(max_write_size, int) and max_write_size > 0:
        if peak > limit_bytes + max_write_size:
            diffs.append(
                f"{side} backpressure_contract.peak_buffered_bytes exceeds soft-limit bound: "
                f"{peak} > {limit_bytes} + {max_write_size}"
            )
    return diffs


def _validate_upload_pause_resume_contract(contract: Dict, *, side: str) -> List[str]:
    diffs: List[str] = []
    if contract.get("schema") != "qcurl-lc/upload-pause-resume@v1":
        return [f"{side} upload_pause_resume.schema mismatch: {contract.get('schema')!r}"]
    payload_size = contract.get("payload_size")
    if not isinstance(payload_size, int) or payload_size <= 0:
        diffs.append(f"{side} upload_pause_resume.payload_size invalid: {payload_size!r}")
    zero_reads = contract.get("zero_read_count")
    if not isinstance(zero_reads, int) or zero_reads <= 0:
        diffs.append(f"{side} upload_pause_resume.zero_read_count invalid: {zero_reads!r}")
    if contract.get("event_seq") != ["pause", "resume"]:
        diffs.append(f"{side} upload_pause_resume.event_seq invalid: {contract.get('event_seq')!r}")
    return diffs


def _compare_optional_contract(
    base: Dict,
    qcurl: Dict,
    *,
    key: str,
    fields: List[str],
) -> tuple[List[str], Dict, Dict]:
    baseline_contract = base.get(key)
    qcurl_contract = qcurl.get(key)
    if baseline_contract is None and qcurl_contract is None:
        return [], {}, {}
    if baseline_contract is None or qcurl_contract is None:
        return [f"{key} missing in one side"], {}, {}
    return compare_dict(baseline_contract, qcurl_contract, fields), baseline_contract, qcurl_contract


def compare_flow_contracts(
    base: Dict,
    qcurl: Dict,
    *,
    baseline_response: Dict,
    qcurl_response: Dict,
) -> List[str]:
    """比较弱/强 pause、backpressure 与上传恢复合同。"""

    diffs, _, _ = _compare_optional_contract(
        base,
        qcurl,
        key="pause_resume",
        fields=["pause_offset", "pause_count", "resume_count", "event_seq"],
    )
    strict_diffs, baseline_strict, qcurl_strict = _compare_optional_contract(
        base,
        qcurl,
        key="pause_resume_strict",
        fields=["schema", "proto", "pause_offset", "resume_delay_ms"],
    )
    diffs.extend(strict_diffs)
    if baseline_strict and qcurl_strict:
        baseline_body_len = int(baseline_response.get("body_len") or 0)
        qcurl_body_len = int(qcurl_response.get("body_len") or 0)
        diffs.extend(
            _validate_pause_resume_contract(
                baseline_strict,
                side="baseline",
                body_len=baseline_body_len,
            )
        )
        diffs.extend(
            _validate_pause_resume_contract(
                qcurl_strict,
                side="qcurl",
                body_len=qcurl_body_len,
            )
        )
    backpressure_diffs, baseline_backpressure, qcurl_backpressure = _compare_optional_contract(
        base,
        qcurl,
        key="backpressure_contract",
        fields=["schema", "proto", "limit_bytes", "resume_bytes", "curl_max_write_size", "event_seq"],
    )
    diffs.extend(backpressure_diffs)
    if baseline_backpressure and qcurl_backpressure:
        diffs.extend(_validate_backpressure_contract(baseline_backpressure, side="baseline"))
        diffs.extend(_validate_backpressure_contract(qcurl_backpressure, side="qcurl"))
    upload_diffs, baseline_upload, qcurl_upload = _compare_optional_contract(
        base,
        qcurl,
        key="upload_pause_resume",
        fields=["schema", "proto", "payload_size", "event_seq"],
    )
    diffs.extend(upload_diffs)
    if baseline_upload and qcurl_upload:
        diffs.extend(_validate_upload_pause_resume_contract(baseline_upload, side="baseline"))
        diffs.extend(_validate_upload_pause_resume_contract(qcurl_upload, side="qcurl"))
    return diffs
