"""
服务端观测解析（LC-5a/5b）：
- httpd access_log：提取 method/path/status/proto 与关键请求头（Range/Content-Length）。
- ws_echo_server handshake log：提取 path 与稳定的握手头白名单。
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional
from urllib.parse import parse_qs, urlencode, urlsplit, urlunsplit


@dataclass(frozen=True)
class HttpdObserved:
    method: str
    url: str  # path(+query) with correlation id stripped
    http_version: str  # "http/1.1" | "h2"
    status: int
    headers: Dict[str, str]


@dataclass(frozen=True)
class WsObserved:
    method: str
    url: str  # path(+query) with correlation id stripped
    headers: Dict[str, str]


def _strip_query_id(url: str) -> str:
    parts = urlsplit(url)
    q = parse_qs(parts.query, keep_blank_values=True)
    q.pop("id", None)
    query = urlencode(q, doseq=True)
    return urlunsplit(("", "", parts.path, query, ""))


def _normalize_http_proto(proto: str) -> str:
    proto = proto.strip()
    if proto.startswith("HTTP/2"):
        return "h2"
    return "http/1.1"

def _normalize_alpn_proto(alpn: str) -> str:
    alpn = (alpn or "").strip().lower()
    if alpn.startswith("h3"):
        return "h3"
    if alpn == "h2":
        return "h2"
    return "http/1.1"


def parse_httpd_access_log(access_log: Path) -> List[Dict[str, str]]:
    """
    解析 httpd access_log（由 curl testenv 写出）。

    当前 LogFormat（见 curl/tests/http/testenv/httpd.py）：
      ts|proto|method|url|status|range|content-length
    """
    if not access_log.exists():
        return []

    entries: List[Dict[str, str]] = []
    for raw in access_log.read_text(encoding="utf-8", errors="replace").splitlines():
        if not raw.strip():
            continue
        parts = raw.split("|")
        if len(parts) != 7:
            continue
        ts, proto, method, url, status, range_v, cl_v = [p.strip() for p in parts]
        u = urlsplit(url)
        q = parse_qs(u.query)
        req_id = q.get("id", [""])[0]
        entries.append({
            "ts": ts,
            "proto": proto,
            "method": method,
            "url": url,
            "status": status,
            "range": range_v,
            "content_length": cl_v,
            "id": req_id,
        })
    return entries


def httpd_observed_for_id(access_log: Path,
                          req_id: str,
                          *,
                          require_range: bool,
                          include_content_length: bool = True) -> HttpdObserved:
    entries = [e for e in parse_httpd_access_log(access_log) if e.get("id") == req_id]
    if not entries:
        raise AssertionError(f"httpd access_log 无匹配记录：id={req_id}")

    def has_range(e: Dict[str, str]) -> bool:
        v = (e.get("range") or "").strip()
        return v not in ("", "-")

    chosen = None
    if require_range:
        for e in entries:
            if has_range(e):
                chosen = e
                break
        if chosen is None:
            raise AssertionError(f"httpd access_log 未观察到 Range 请求：id={req_id}")
        # 中断+续传应至少包含一次非 Range 与一次 Range
        if not any(not has_range(e) for e in entries):
            raise AssertionError(f"httpd access_log 未观察到首段（非 Range）请求：id={req_id}")
    else:
        chosen = entries[0]

    headers: Dict[str, str] = {}
    range_v = (chosen.get("range") or "").strip()
    if range_v not in ("", "-"):
        headers["range"] = range_v
    if include_content_length:
        cl_v = (chosen.get("content_length") or "").strip()
        if cl_v not in ("", "-"):
            headers["content-length"] = cl_v

    return HttpdObserved(
        method=(chosen.get("method") or "").upper(),
        url=_strip_query_id(chosen.get("url") or ""),
        http_version=_normalize_http_proto(chosen.get("proto") or ""),
        status=int(chosen.get("status") or "0"),
        headers=headers,
    )

def httpd_observed_list_for_id(access_log: Path,
                               req_id: str,
                               *,
                               expected_count: int,
                               include_content_length: bool = True) -> List[HttpdObserved]:
    """
    返回同一 correlation id 下的所有 HTTP 请求观测记录。
    - 用于 multi 场景（同一 case 内多次请求）
    - 返回结果按 url（去掉 id 后）排序以稳定对比
    """
    entries = [e for e in parse_httpd_access_log(access_log) if e.get("id") == req_id]
    if not entries:
        raise AssertionError(f"httpd access_log 无匹配记录：id={req_id}")
    if expected_count > 0 and len(entries) != expected_count:
        raise AssertionError(f"httpd access_log 记录数不匹配：id={req_id}, got={len(entries)}, expected={expected_count}")

    observed: List[HttpdObserved] = []
    for e in entries:
        headers: Dict[str, str] = {}
        range_v = (e.get("range") or "").strip()
        if range_v not in ("", "-"):
            headers["range"] = range_v
        if include_content_length:
            cl_v = (e.get("content_length") or "").strip()
            if cl_v not in ("", "-"):
                headers["content-length"] = cl_v
        observed.append(HttpdObserved(
            method=(e.get("method") or "").upper(),
            url=_strip_query_id(e.get("url") or ""),
            http_version=_normalize_http_proto(e.get("proto") or ""),
            status=int(e.get("status") or "0"),
            headers=headers,
        ))
    return sorted(observed, key=lambda x: x.url)

def parse_nghttpx_access_log(access_log: Path) -> List[Dict[str, str]]:
    """
    解析 nghttpx access_log（由 curl testenv 写出）。

    当前 LogFormat（见 curl/tests/http/testenv/nghttpx.py）：
      ts|alpn|method|path|status|range|content-length
    """
    if not access_log.exists():
        return []

    entries: List[Dict[str, str]] = []
    for raw in access_log.read_text(encoding="utf-8", errors="replace").splitlines():
        if not raw.strip():
            continue
        parts = raw.split("|")
        if len(parts) != 7:
            continue
        ts, alpn, method, path, status, range_v, cl_v = [p.strip() for p in parts]
        u = urlsplit(path)
        q = parse_qs(u.query)
        req_id = q.get("id", [""])[0]
        entries.append({
            "ts": ts,
            "alpn": alpn,
            "method": method,
            "path": path,
            "status": status,
            "range": range_v,
            "content_length": cl_v,
            "id": req_id,
        })
    return entries


def nghttpx_observed_for_id(access_log: Path,
                            req_id: str,
                            *,
                            require_range: bool,
                            include_content_length: bool = True) -> HttpdObserved:
    entries = [e for e in parse_nghttpx_access_log(access_log) if e.get("id") == req_id]
    if not entries:
        raise AssertionError(f"nghttpx access_log 无匹配记录：id={req_id}")

    def has_range(e: Dict[str, str]) -> bool:
        v = (e.get("range") or "").strip()
        return v not in ("", "-")

    chosen = None
    if require_range:
        for e in entries:
            if has_range(e):
                chosen = e
                break
        if chosen is None:
            raise AssertionError(f"nghttpx access_log 未观察到 Range 请求：id={req_id}")
        if not any(not has_range(e) for e in entries):
            raise AssertionError(f"nghttpx access_log 未观察到首段（非 Range）请求：id={req_id}")
    else:
        chosen = entries[0]

    headers: Dict[str, str] = {}
    range_v = (chosen.get("range") or "").strip()
    if range_v not in ("", "-"):
        headers["range"] = range_v
    if include_content_length:
        cl_v = (chosen.get("content_length") or "").strip()
        if cl_v not in ("", "-"):
            headers["content-length"] = cl_v

    return HttpdObserved(
        method=(chosen.get("method") or "").upper(),
        url=_strip_query_id(chosen.get("path") or ""),
        http_version=_normalize_alpn_proto(chosen.get("alpn") or ""),
        status=int(chosen.get("status") or "0"),
        headers=headers,
    )

def nghttpx_observed_list_for_id(access_log: Path,
                                 req_id: str,
                                 *,
                                 expected_count: int,
                                 include_content_length: bool = True) -> List[HttpdObserved]:
    """
    返回同一 correlation id 下的所有 HTTP/3 请求观测记录（来自 nghttpx access_log）。
    - 返回结果按 url（去掉 id 后）排序以稳定对比
    """
    entries = [e for e in parse_nghttpx_access_log(access_log) if e.get("id") == req_id]
    if not entries:
        raise AssertionError(f"nghttpx access_log 无匹配记录：id={req_id}")
    if expected_count > 0 and len(entries) != expected_count:
        raise AssertionError(f"nghttpx access_log 记录数不匹配：id={req_id}, got={len(entries)}, expected={expected_count}")

    observed: List[HttpdObserved] = []
    for e in entries:
        headers: Dict[str, str] = {}
        range_v = (e.get("range") or "").strip()
        if range_v not in ("", "-"):
            headers["range"] = range_v
        if include_content_length:
            cl_v = (e.get("content_length") or "").strip()
            if cl_v not in ("", "-"):
                headers["content-length"] = cl_v
        observed.append(HttpdObserved(
            method=(e.get("method") or "").upper(),
            url=_strip_query_id(e.get("path") or ""),
            http_version=_normalize_alpn_proto(e.get("alpn") or ""),
            status=int(e.get("status") or "0"),
            headers=headers,
        ))
    return sorted(observed, key=lambda x: x.url)


def parse_ws_handshake_log(path: Path) -> List[Dict]:
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


def ws_observed_for_id(handshake_log: Path, req_id: str) -> WsObserved:
    entries = [e for e in parse_ws_handshake_log(handshake_log) if (e.get("id") or "") == req_id]
    if not entries:
        raise AssertionError(f"ws handshake log 无匹配记录：id={req_id}")
    e = entries[0]
    path = e.get("path") or "/"
    headers_in = e.get("headers") or {}
    headers = {str(k).lower(): str(v) for k, v in headers_in.items()}
    return WsObserved(
        method="GET",
        url=_strip_query_id(path),
        headers=headers,
    )
