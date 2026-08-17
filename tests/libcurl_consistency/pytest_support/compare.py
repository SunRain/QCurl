"""
artifacts 对比器（LC-5）：
- 按 LC-0 强制字段比较：request 语义摘要、response 长度/sha256/status/http_version。
- 可返回 diff 列表供 pytest 断言或报告输出。
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Dict, List, Mapping, Tuple

from .compare_flow_contracts import compare_flow_contracts
from .compare_sections import compare_sections
from .compare_shared import validate_required_fields


_DEFAULT_REQUIRED_FIELDS: dict[str, type | tuple[type, ...]] = {
    "request.method": str,
    "request.url": str,
    "request.headers": dict,
    "request.body_len": int,
    "request.body_sha256": str,
    "response.status": int,
    "response.http_version": str,
    "response.headers": dict,
    "response.body_len": int,
    "response.body_sha256": str,
}


def _load(path: Path) -> Dict:
    return json.loads(path.read_text(encoding="utf-8"))


def compare_artifacts(
    baseline_path: Path,
    qcurl_path: Path,
    *,
    required_fields: Mapping[str, type | tuple[type, ...]] | None = None,
) -> Tuple[bool, List[str]]:
    """比较 baseline 与 QCurl artifacts，返回是否一致与差异列表。"""

    baseline = _load(baseline_path)
    qcurl = _load(qcurl_path)
    required = required_fields if required_fields is not None else _DEFAULT_REQUIRED_FIELDS
    diffs = validate_required_fields(
        baseline,
        side="baseline",
        required_fields=required,
    )
    diffs.extend(
        validate_required_fields(
            qcurl,
            side="qcurl",
            required_fields=required,
        )
    )
    section_diffs, baseline_response, qcurl_response = compare_sections(baseline, qcurl)
    diffs.extend(section_diffs)
    diffs.extend(
        compare_flow_contracts(
            baseline,
            qcurl,
            baseline_response=baseline_response,
            qcurl_response=qcurl_response,
        )
    )
    return not diffs, diffs


def assert_artifacts_match(
    baseline_path: Path,
    qcurl_path: Path,
    *,
    required_fields: Mapping[str, type | tuple[type, ...]] | None = None,
) -> None:
    """pytest 断言辅助：不一致时抛出 AssertionError，并列出差异。"""

    ok, diffs = compare_artifacts(
        baseline_path,
        qcurl_path,
        required_fields=required_fields,
    )
    if not ok:
        detail = "\n".join(diffs)
        raise AssertionError(f"Artifacts mismatch:\n{detail}")
