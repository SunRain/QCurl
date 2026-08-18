"""一致性 gate 执行计划的内容封签与校验。"""

from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from typing import Any


def _canonical_bytes(plan: dict[str, Any]) -> bytes:
    payload = {key: value for key, value in plan.items() if key != "content_sha256"}
    return json.dumps(
        payload,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")


def seal_execution_plan(plan: dict[str, Any]) -> dict[str, Any]:
    """复制并封签本轮派生执行计划。"""

    sealed = deepcopy(plan)
    sealed["schema"] = "qcurl-lc/execution-plan@v1"
    sealed["content_sha256"] = hashlib.sha256(_canonical_bytes(sealed)).hexdigest()
    return sealed


def validate_execution_plan(
    plan: dict[str, Any],
    *,
    expected_run_id: str,
    expected_execution_token: str,
    expected_identity: dict[str, object],
) -> list[str]:
    """校验执行计划的封签与本轮运行身份。"""

    errors: list[str] = []
    if plan.get("schema") != "qcurl-lc/execution-plan@v1":
        errors.append("schema mismatch")
    expected_hash = hashlib.sha256(_canonical_bytes(plan)).hexdigest()
    if plan.get("content_sha256") != expected_hash:
        errors.append("content hash mismatch")
    if plan.get("run_id") != expected_run_id:
        errors.append("run_id mismatch")
    if plan.get("execution_token") != expected_execution_token:
        errors.append("execution_token mismatch")
    if plan.get("identity") != expected_identity:
        errors.append("identity mismatch")
    return errors
