"""核验 gate 报告内嵌证据与独立证据文件的一致性。"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from .capability_manifest import validate_manifest
from .execution_plan import validate_execution_plan


def _prefixed(label: str, errors: list[str]) -> list[str]:
    return [f"{label}: {error}" for error in errors]


def _read_json_object(path: Path) -> tuple[dict[str, Any], list[str]]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        return {}, [f"{path}: {exc}"]
    if not isinstance(value, dict):
        return {}, [f"{path}: root must be an object"]
    return value, []


def verify_gate_evidence_integrity(
    *,
    standalone_manifest: dict[str, Any],
    embedded_manifest: dict[str, Any],
    standalone_plan: dict[str, Any],
    embedded_plan: dict[str, Any],
    expected_identity: dict[str, object],
    gate_started_epoch: float,
    now_epoch: float,
    expected_run_id: str,
    expected_execution_token: str,
) -> dict[str, object]:
    """校验 capability 与 execution plan 的独立副本和内嵌副本。"""

    manifest_args = {
        "expected_identity": expected_identity,
        "gate_started_epoch": gate_started_epoch,
        "now_epoch": now_epoch,
    }
    plan_identity = {
        "source": expected_identity.get("source", {}),
        "build": expected_identity.get("build", {}),
    }
    plan_args = {
        "expected_run_id": expected_run_id,
        "expected_execution_token": expected_execution_token,
        "expected_identity": plan_identity,
    }
    errors = _prefixed(
        "standalone_manifest",
        validate_manifest(standalone_manifest, **manifest_args),
    )
    errors.extend(_prefixed(
        "embedded_manifest",
        validate_manifest(embedded_manifest, **manifest_args),
    ))
    errors.extend(_prefixed(
        "standalone_plan",
        validate_execution_plan(standalone_plan, **plan_args),
    ))
    errors.extend(_prefixed(
        "embedded_plan",
        validate_execution_plan(embedded_plan, **plan_args),
    ))
    if standalone_manifest != embedded_manifest:
        errors.append("capability manifest copies differ")
    if standalone_plan != embedded_plan:
        errors.append("execution plan copies differ")
    return {"valid": not errors, "errors": errors}


def verify_gate_evidence_files(
    *,
    manifest_path: Path,
    embedded_manifest: dict[str, Any],
    plan_path: Path,
    embedded_plan: dict[str, Any],
    expected_identity: dict[str, object],
    gate_started_epoch: float,
    now_epoch: float,
    expected_run_id: str,
    expected_execution_token: str,
) -> dict[str, object]:
    """读取独立证据文件并与报告内嵌副本一起校验。"""

    standalone_manifest, errors = _read_json_object(manifest_path)
    standalone_plan, plan_errors = _read_json_object(plan_path)
    errors.extend(plan_errors)
    result = verify_gate_evidence_integrity(
        standalone_manifest=standalone_manifest,
        embedded_manifest=embedded_manifest,
        standalone_plan=standalone_plan,
        embedded_plan=embedded_plan,
        expected_identity=expected_identity,
        gate_started_epoch=gate_started_epoch,
        now_epoch=now_epoch,
        expected_run_id=expected_run_id,
        expected_execution_token=expected_execution_token,
    )
    result["errors"] = [*errors, *result["errors"]]
    result["valid"] = not result["errors"]
    return result
