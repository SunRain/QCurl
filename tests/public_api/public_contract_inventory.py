"""Validate the machine-readable installed public contract inventory."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


AXES = ("fallible", "errorLifecycle", "qobjectBorrow", "owningSmartPointer")
AXIS_STATES = {"none", "documented", "contracts"}
CONTRACT_CATEGORIES = set(AXES)
DISPOSITIONS = {"keep", "hard-break", "document", "remove"}
PHASES = {"T5", "T6", "T7", "T8"}


def _load_json(path: Path, errors: list[str]) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"{path}: cannot load JSON: {exc}")
        return {}
    if not isinstance(value, dict):
        errors.append(f"{path}: root must be an object")
        return {}
    return value


def _manifest_headers(manifest: dict[str, Any]) -> set[str]:
    return {
        item["path"]
        for item in manifest.get("headers", [])
        if isinstance(item, dict) and item.get("visibility") != "Internal" and "path" in item
    }


def _validate_contract(
    contract: object,
    installed_headers: set[str],
    source_root: Path,
    errors: list[str],
) -> str | None:
    if not isinstance(contract, dict):
        errors.append("inventory contract entry must be an object")
        return None

    required = {
        "id",
        "category",
        "header",
        "symbol",
        "currentContract",
        "targetContract",
        "stateAndLifetime",
        "threadContract",
        "disposition",
        "phase",
    }
    missing = sorted(required - contract.keys())
    contract_id = contract.get("id")
    label = contract_id if isinstance(contract_id, str) else "<missing-id>"
    if missing:
        errors.append(f"{label}: missing fields: {', '.join(missing)}")
    if not isinstance(contract_id, str) or not contract_id:
        errors.append("contract id must be a non-empty string")
        return None

    header = contract.get("header")
    if header not in installed_headers:
        errors.append(f"{contract_id}: header is not installed: {header}")
    elif not (source_root / str(header)).is_file():
        errors.append(f"{contract_id}: source header does not exist: {header}")

    if contract.get("category") not in CONTRACT_CATEGORIES:
        errors.append(f"{contract_id}: invalid category: {contract.get('category')}")
    if contract.get("disposition") not in DISPOSITIONS:
        errors.append(f"{contract_id}: invalid disposition: {contract.get('disposition')}")
    if contract.get("phase") not in PHASES:
        errors.append(f"{contract_id}: invalid phase: {contract.get('phase')}")

    for field in required - {"id", "header"}:
        value = contract.get(field)
        if not isinstance(value, str) or not value.strip():
            errors.append(f"{contract_id}: {field} must be a non-empty string")

    header_source = ""
    if isinstance(header, str) and (source_root / header).is_file():
        header_source = (source_root / header).read_text(encoding="utf-8")

    semantic_anchors = contract.get("semanticAnchors")
    if semantic_anchors is not None:
        if not isinstance(semantic_anchors, dict):
            errors.append(f"{contract_id}: semanticAnchors must be an object")
        else:
            _validate_semantic_anchors(
                contract_id,
                contract,
                header_source,
                semantic_anchors,
                errors,
            )
    return contract_id


def _validate_string_array(
    contract_id: str,
    field: str,
    value: object,
    errors: list[str],
) -> list[str]:
    if not isinstance(value, list) or not value or not all(
        isinstance(item, str) and item for item in value
    ):
        errors.append(f"{contract_id}: semanticAnchors.{field} must be a non-empty string array")
        return []
    return value


def _validate_semantic_anchors(
    contract_id: str,
    contract: dict[str, Any],
    header_source: str,
    semantic_anchors: dict[str, Any],
    errors: list[str],
) -> None:
    allowed = {"currentContract", "headerIncludes", "headerExcludes"}
    unknown = sorted(set(semantic_anchors) - allowed)
    if unknown:
        errors.append(f"{contract_id}: unknown semantic anchor fields: {', '.join(unknown)}")

    current_contract = str(contract.get("currentContract", ""))
    for anchor in _validate_string_array(
        contract_id,
        "currentContract",
        semantic_anchors.get("currentContract"),
        errors,
    ):
        if anchor not in current_contract:
            errors.append(f"{contract_id}: currentContract misses semantic anchor: {anchor}")

    for anchor in _validate_string_array(
        contract_id,
        "headerIncludes",
        semantic_anchors.get("headerIncludes"),
        errors,
    ):
        if anchor not in header_source:
            errors.append(f"{contract_id}: public header misses semantic anchor: {anchor}")

    for anchor in _validate_string_array(
        contract_id,
        "headerExcludes",
        semantic_anchors.get("headerExcludes"),
        errors,
    ):
        if anchor in header_source:
            errors.append(f"{contract_id}: public header contains forbidden anchor: {anchor}")


def validate_public_contract_inventory(
    inventory_path: Path,
    surface_manifest_path: Path,
    source_root: Path,
) -> list[str]:
    """Return every inventory consistency error; an empty list means complete."""

    errors: list[str] = []
    inventory = _load_json(inventory_path, errors)
    manifest = _load_json(surface_manifest_path, errors)
    if errors:
        return errors

    if inventory.get("schemaVersion") != 1:
        errors.append("inventory schemaVersion must be 1")

    installed_headers = _manifest_headers(manifest)
    contracts_by_id: dict[str, dict[str, Any]] = {}
    for contract in inventory.get("contracts", []):
        contract_id = _validate_contract(contract, installed_headers, source_root, errors)
        if contract_id is None:
            continue
        if contract_id in contracts_by_id:
            errors.append(f"duplicate contract id: {contract_id}")
            continue
        contracts_by_id[contract_id] = contract

    axis_reviews = inventory.get("axisReviews")
    if not isinstance(axis_reviews, dict):
        return errors + ["inventory axisReviews must be an object"]

    referenced_contracts: set[str] = set()
    for axis in AXES:
        review = axis_reviews.get(axis)
        if not isinstance(review, dict):
            errors.append(f"axisReviews.{axis} must be an object")
            continue

        none_headers = review.get("none", [])
        documented_headers = review.get("documented", [])
        contract_headers = review.get("contracts", {})
        if not isinstance(none_headers, list) or not all(
            isinstance(item, str) for item in none_headers
        ):
            errors.append(f"axisReviews.{axis}.none must be a string array")
            none_headers = []
        if not isinstance(documented_headers, list) or not all(
            isinstance(item, str) for item in documented_headers
        ):
            errors.append(f"axisReviews.{axis}.documented must be a string array")
            documented_headers = []
        if not isinstance(contract_headers, dict):
            errors.append(f"axisReviews.{axis}.contracts must be an object")
            contract_headers = {}

        classified = list(none_headers) + list(documented_headers) + list(contract_headers)
        duplicates = sorted({header for header in classified if classified.count(header) > 1})
        if duplicates:
            errors.append(f"{axis} classifies headers more than once: {', '.join(duplicates)}")
        missing_headers = sorted(installed_headers - set(classified))
        extra_headers = sorted(set(classified) - installed_headers)
        if missing_headers:
            errors.append(f"{axis} misses installed headers: {', '.join(missing_headers)}")
        if extra_headers:
            errors.append(f"{axis} reviews non-installed headers: {', '.join(extra_headers)}")

        for header, ids in contract_headers.items():
            if not isinstance(ids, list) or not ids or not all(isinstance(item, str) for item in ids):
                errors.append(f"{header}: {axis} contracts must be a non-empty string array")
                continue
            for contract_id in ids:
                contract = contracts_by_id.get(contract_id)
                if contract is None:
                    errors.append(f"{header}: unknown contract id: {contract_id}")
                    continue
                if contract.get("header") != header or contract.get("category") != axis:
                    errors.append(f"{header}: {axis} references mismatched contract: {contract_id}")
                referenced_contracts.add(contract_id)

    orphaned = sorted(set(contracts_by_id) - referenced_contracts)
    if orphaned:
        errors.append("inventory has unreferenced contracts: " + ", ".join(orphaned))

    allowlist = inventory.get("hardBreakAllowlist")
    if not isinstance(allowlist, list) or not all(isinstance(item, str) for item in allowlist):
        errors.append("hardBreakAllowlist must be a string array")
    else:
        expected = {
            contract_id
            for contract_id, contract in contracts_by_id.items()
            if contract.get("phase") == "T6" and contract.get("disposition") == "hard-break"
        }
        if set(allowlist) != expected or len(allowlist) != len(set(allowlist)):
            errors.append("hardBreakAllowlist must exactly match unique T6 hard-break contracts")
    return errors
