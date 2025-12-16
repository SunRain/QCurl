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


def _cmp_dict(lhs: Dict, rhs: Dict, fields: List[str]) -> List[str]:
    diffs = []
    for f in fields:
        if lhs.get(f) != rhs.get(f):
            diffs.append(f"{f} mismatch: {lhs.get(f)} != {rhs.get(f)}")
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
    b_req = base.get("request")
    q_req = qc.get("request")
    if not b_req or not q_req:
        diffs.append("request missing in one side")
    else:
        diffs.extend(_cmp_dict(b_req, q_req, ["method", "url", "headers", "body_len", "body_sha256"]))

    # 响应摘要（或下载文件摘要）
    b_resp = base.get("response")
    q_resp = qc.get("response")
    if not b_resp or not q_resp:
        diffs.append("response missing in one side")
    else:
        diffs.extend(_cmp_dict(b_resp, q_resp, ["status", "http_version", "headers", "body_len", "body_sha256"]))

    # 可选：cookie jar 输出一致性（P1）
    b_cookiejar = base.get("cookiejar")
    q_cookiejar = qc.get("cookiejar")
    if b_cookiejar is not None or q_cookiejar is not None:
        if not b_cookiejar or not q_cookiejar:
            diffs.append("cookiejar missing in one side")
        else:
            diffs.extend(_cmp_dict(b_cookiejar, q_cookiejar, ["records", "sha256"]))

    return (len(diffs) == 0, diffs)


def assert_artifacts_match(baseline_path: Path, qcurl_path: Path) -> None:
    """pytest 断言辅助：不一致时抛出 AssertionError，并列出差异。"""
    ok, diffs = compare_artifacts(baseline_path, qcurl_path)
    if not ok:
        detail = "\n".join(diffs)
        raise AssertionError(f"Artifacts mismatch:\n{detail}")
