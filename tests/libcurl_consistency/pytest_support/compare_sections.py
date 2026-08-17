"""HTTP 与可选观测字段的一致性比较。"""

from __future__ import annotations

from typing import Dict, List, Tuple

from .compare_shared import compare_dict
from .compare_shared import compare_list_dict
from .compare_shared import compare_optional_fields
from .compare_shared import extract_error_namespaces


def _compare_requests(base: Dict, qcurl: Dict) -> List[str]:
    diffs: List[str] = []
    baseline_requests = base.get("requests")
    qcurl_requests = qcurl.get("requests")
    if baseline_requests is not None or qcurl_requests is not None:
        if not baseline_requests or not qcurl_requests:
            return ["requests missing in one side"]
        return compare_list_dict(
            baseline_requests,
            qcurl_requests,
            ["method", "url", "headers", "body_len", "body_sha256"],
            "requests",
        )

    baseline_request = base.get("request")
    qcurl_request = qcurl.get("request")
    if not baseline_request or not qcurl_request:
        return ["request missing in one side"]
    diffs.extend(
        compare_dict(
            baseline_request,
            qcurl_request,
            ["method", "url", "headers", "body_len", "body_sha256"],
        )
    )
    diffs.extend(
        compare_optional_fields(
            baseline_request,
            qcurl_request,
            ["headers_raw_len", "headers_raw_sha256", "headers_raw_lines", "headers_semantic"],
            "request",
        )
    )
    return diffs


def _compare_responses(base: Dict, qcurl: Dict) -> tuple[List[str], Dict, Dict]:
    diffs: List[str] = []
    baseline_responses = base.get("responses")
    qcurl_responses = qcurl.get("responses")
    if baseline_responses is not None or qcurl_responses is not None:
        if not baseline_responses or not qcurl_responses:
            diffs.append("responses missing in one side")
        else:
            diffs.extend(
                compare_list_dict(
                    baseline_responses,
                    qcurl_responses,
                    ["status", "http_version", "headers", "body_len", "body_sha256"],
                    "responses",
                )
            )

    baseline_response = base.get("response")
    qcurl_response = qcurl.get("response")
    if not baseline_response or not qcurl_response:
        diffs.append("response missing in one side")
    else:
        diffs.extend(
            compare_dict(
                baseline_response,
                qcurl_response,
                ["status", "http_version", "headers", "body_len", "body_sha256"],
            )
        )
        diffs.extend(
            compare_optional_fields(
                baseline_response,
                qcurl_response,
                ["headers_raw_len", "headers_raw_sha256", "headers_raw_lines"],
                "response",
            )
        )
    return diffs, baseline_response or {}, qcurl_response or {}


def _compare_cookiejar(base: Dict, qcurl: Dict) -> List[str]:
    baseline_cookiejar = base.get("cookiejar")
    qcurl_cookiejar = qcurl.get("cookiejar")
    if baseline_cookiejar is None and qcurl_cookiejar is None:
        return []
    if not baseline_cookiejar or not qcurl_cookiejar:
        return ["cookiejar missing in one side"]
    return compare_dict(baseline_cookiejar, qcurl_cookiejar, ["records", "sha256"])


def _compare_errors(base: Dict, qcurl: Dict) -> List[str]:
    diffs: List[str] = []
    baseline_observed, baseline_derived = extract_error_namespaces(base)
    qcurl_observed, qcurl_derived = extract_error_namespaces(qcurl)
    baseline_has_error = bool(baseline_observed or baseline_derived)
    qcurl_has_error = bool(qcurl_observed or qcurl_derived)
    if not baseline_has_error and not qcurl_has_error:
        return diffs
    if not baseline_has_error or not qcurl_has_error:
        return ["error missing in one side"]

    for field in ("http_status", "http_code"):
        baseline_has = field in baseline_observed
        qcurl_has = field in qcurl_observed
        if baseline_has != qcurl_has:
            diffs.append(f"observed.error.{field} missing in one side")
        elif baseline_has and baseline_observed.get(field) != qcurl_observed.get(field):
            diffs.append(
                f"observed.error.{field} mismatch: "
                f"{baseline_observed.get(field)} != {qcurl_observed.get(field)}"
            )
    for field in ("kind", "curlcode"):
        baseline_has = field in baseline_derived
        qcurl_has = field in qcurl_derived
        if baseline_has != qcurl_has:
            diffs.append(f"derived.error.{field} missing in one side")
        elif baseline_has and baseline_derived.get(field) != qcurl_derived.get(field):
            diffs.append(
                f"derived.error.{field} mismatch: "
                f"{baseline_derived.get(field)} != {qcurl_derived.get(field)}"
            )
    return diffs


def _compare_mapping_section(
    base: Dict,
    qcurl: Dict,
    *,
    key: str,
    fields: List[str],
) -> List[str]:
    baseline_value = base.get(key)
    qcurl_value = qcurl.get(key)
    if baseline_value is None and qcurl_value is None:
        return []
    if not isinstance(baseline_value, dict) or not isinstance(qcurl_value, dict):
        return [f"{key} missing in one side"]
    return compare_dict(baseline_value, qcurl_value, fields)


def _compare_transport(base: Dict, qcurl: Dict) -> List[str]:
    baseline_transport = base.get("transport")
    qcurl_transport = qcurl.get("transport")
    if baseline_transport is None and qcurl_transport is None:
        return []
    if not isinstance(baseline_transport, dict) or not isinstance(qcurl_transport, dict):
        return ["transport missing in one side"]
    if baseline_transport != qcurl_transport:
        return [f"transport mismatch: {baseline_transport!r} != {qcurl_transport!r}"]
    return []


def _compare_progress(base: Dict, qcurl: Dict) -> List[str]:
    diffs: List[str] = []
    baseline_progress = base.get("progress_summary")
    qcurl_progress = qcurl.get("progress_summary")
    if baseline_progress is None and qcurl_progress is None:
        return diffs
    if baseline_progress is None or qcurl_progress is None:
        return ["progress_summary missing in one side"]
    for lane in ("download", "upload"):
        baseline_lane = baseline_progress.get(lane)
        qcurl_lane = qcurl_progress.get(lane)
        if baseline_lane is None and qcurl_lane is None:
            continue
        if baseline_lane is None or qcurl_lane is None:
            diffs.append(f"progress_summary.{lane} missing in one side")
            continue
        diffs.extend(
            compare_dict(
                baseline_lane,
                qcurl_lane,
                ["monotonic", "now_max", "total_max"],
            )
        )
    return diffs


def compare_sections(base: Dict, qcurl: Dict) -> Tuple[List[str], Dict, Dict]:
    """比较常规 HTTP、错误和传输观测，返回响应供流控合同复用。"""

    diffs = _compare_requests(base, qcurl)
    response_diffs, baseline_response, qcurl_response = _compare_responses(base, qcurl)
    diffs.extend(response_diffs)
    diffs.extend(_compare_cookiejar(base, qcurl))
    diffs.extend(_compare_errors(base, qcurl))
    diffs.extend(
        _compare_mapping_section(
            base,
            qcurl,
            key="socks",
            fields=["version", "cmd", "atyp", "dst", "dst_port", "rep"],
        )
    )
    diffs.extend(
        _compare_mapping_section(
            base,
            qcurl,
            key="protocol",
            fields=["requested", "observed"],
        )
    )
    diffs.extend(_compare_transport(base, qcurl))
    diffs.extend(_compare_progress(base, qcurl))
    diffs.extend(
        _compare_mapping_section(
            base,
            qcurl,
            key="connection_observed",
            fields=["request_count", "unique_connections", "conn_seq"],
        )
    )
    return diffs, baseline_response, qcurl_response
