"""Validate the QCurl public surface manifest consumed by public-api gates."""

from __future__ import annotations

import json
from argparse import Namespace
from pathlib import Path
from typing import Any
from typing import Callable


FailFunc = Callable[[str], int]

ALLOWED_LAYERS = {
    "Core",
    "Blocking Extras",
    "Other Extras",
    "Test Support",
    "Internal",
}

ALLOWED_INSTALLS = {
    "core-default",
    "blocking-extras",
    "other-extras",
    "test-support",
    "conditional-extras",
    "none",
}


def _read_lines(path: Path) -> set[str]:
    return {line.strip() for line in path.read_text(encoding="utf-8").splitlines() if line.strip()}


def _load_json(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise RuntimeError("surface manifest root must be an object")
    return data


def _collect_header_entries(data: dict[str, Any], errors: list[str]) -> dict[str, dict[str, Any]]:
    raw_entries = data.get("headers")
    if not isinstance(raw_entries, list) or not raw_entries:
        errors.append("headers must be a non-empty array")
        return {}

    entries: dict[str, dict[str, Any]] = {}
    for index, entry in enumerate(raw_entries):
        if not isinstance(entry, dict):
            errors.append(f"headers[{index}] must be an object")
            continue

        path = entry.get("path")
        if not isinstance(path, str) or not path.strip():
            errors.append(f"headers[{index}].path must be a non-empty string")
            continue

        normalized_path = path.strip()
        if normalized_path in entries:
            errors.append(f"{normalized_path}: duplicate manifest header entry")
            continue

        entries[normalized_path] = entry

    return entries


def _validate_entry_values(entries: dict[str, dict[str, Any]], errors: list[str]) -> None:
    for path, entry in entries.items():
        layer = entry.get("layer")
        current_install = entry.get("currentInstall", "none")
        target_install = entry.get("targetInstall", "none")

        if layer not in ALLOWED_LAYERS:
            errors.append(f"{path}: invalid layer {layer!r}")
        if current_install not in ALLOWED_INSTALLS:
            errors.append(f"{path}: invalid currentInstall {current_install!r}")
        if target_install not in ALLOWED_INSTALLS:
            errors.append(f"{path}: invalid targetInstall {target_install!r}")
        if layer == "Internal" and current_install != "none":
            errors.append(f"{path}: Internal header must not have a public install surface")
        if layer in {"Blocking Extras", "Other Extras", "Test Support"} and target_install == "core-default":
            errors.append(f"{path}: non-Core layer must not target the default Core install")


def _validate_schema(data: dict[str, Any], errors: list[str]) -> None:
    if data.get("schemaVersion") != 1:
        errors.append("schemaVersion must be 1")

    layers = data.get("layers")
    if not isinstance(layers, list) or set(layers) != ALLOWED_LAYERS:
        errors.append("layers must list Core, Blocking Extras, Other Extras, Test Support, Internal")


def _validate_core_manifest(
    core_headers: set[str],
    entries: dict[str, dict[str, Any]],
    errors: list[str],
) -> None:
    for header in sorted(core_headers):
        entry = entries.get(header)
        if entry is None:
            errors.append(f"{header}: missing from surface manifest")
            continue

        if entry.get("currentInstall") != "core-default":
            errors.append(f"{header}: currentInstall must match generated Core manifest")
        if entry.get("layer") == "Internal":
            errors.append(f"{header}: Internal header leaked into generated Core manifest")
        if entry.get("layer") != "Core" and not entry.get("extractionTask"):
            errors.append(f"{header}: non-Core default install must record extractionTask")


def _validate_extras_manifest(
    extras_headers: set[str],
    entries: dict[str, dict[str, Any]],
    errors: list[str],
) -> None:
    for header in sorted(extras_headers):
        entry = entries.get(header)
        if entry is None:
            errors.append(f"{header}: missing from surface manifest")
            continue

        if entry.get("layer") not in {"Other Extras", "Blocking Extras", "Test Support"}:
            errors.append(f"{header}: generated extras manifest must not contain Core/Internal layer")
        if entry.get("targetInstall") == "core-default":
            errors.append(f"{header}: extras manifest entry must not target default Core install")

        expected_install_by_layer = {
            "Blocking Extras": "blocking-extras",
            "Test Support": "test-support",
        }
        expected_install = expected_install_by_layer.get(entry.get("layer"))
        if expected_install and entry.get("currentInstall") != expected_install:
            errors.append(f"{header}: currentInstall must be {expected_install} for generated extras manifest")


def _validate_blocking_extras_defined(
    data: dict[str, Any],
    entries: dict[str, dict[str, Any]],
    errors: list[str],
) -> None:
    has_installed_blocking_extras = any(
        entry.get("layer") == "Blocking Extras" and entry.get("targetInstall") == "blocking-extras"
        for entry in entries.values()
    )

    planned = data.get("plannedHeaders", [])
    if not isinstance(planned, list):
        errors.append("plannedHeaders must be an array when present")
        return

    has_planned_blocking_extras = any(
        isinstance(entry, dict) and entry.get("layer") == "Blocking Extras" for entry in planned
    )
    if not has_installed_blocking_extras and not has_planned_blocking_extras:
        errors.append("surface manifest must include installed or planned Blocking Extras headers")


def _validate_symbol_extractions(
    data: dict[str, Any],
    entries: dict[str, dict[str, Any]],
    errors: list[str],
) -> None:
    extractions = data.get("symbolExtractions", [])
    if not isinstance(extractions, list):
        errors.append("symbolExtractions must be an array when present")
        return

    for index, item in enumerate(extractions):
        if not isinstance(item, dict):
            errors.append(f"symbolExtractions[{index}] must be an object")
            continue

        from_header = item.get("fromHeader")
        symbols = item.get("symbols")
        target_layer = item.get("targetLayer")
        if from_header not in entries:
            errors.append(f"symbolExtractions[{index}].fromHeader must reference a manifest header")
        if not isinstance(symbols, list) or not all(isinstance(symbol, str) for symbol in symbols):
            errors.append(f"symbolExtractions[{index}].symbols must be a string array")
        if target_layer not in ALLOWED_LAYERS - {"Core", "Internal"}:
            errors.append(f"symbolExtractions[{index}].targetLayer must be a public non-Core layer")
        if not item.get("extractionTask"):
            errors.append(f"symbolExtractions[{index}].extractionTask is required")


def validate_surface_manifest(args: Namespace, *, fail_func: FailFunc) -> int:
    """Validate generated install manifests against the machine-readable surface manifest."""

    try:
        data = _load_json(args.surface_manifest)
        core_headers = _read_lines(args.core_manifest)
        extras_headers = _read_lines(args.extras_manifest)
    except (OSError, json.JSONDecodeError, RuntimeError) as exc:
        return fail_func(f"surface manifest read failed: {exc}")

    errors: list[str] = []
    _validate_schema(data, errors)
    entries = _collect_header_entries(data, errors)
    _validate_entry_values(entries, errors)
    _validate_core_manifest(core_headers, entries, errors)
    _validate_extras_manifest(extras_headers, entries, errors)
    _validate_blocking_extras_defined(data, entries, errors)
    _validate_symbol_extractions(data, entries, errors)

    if errors:
        return fail_func("surface manifest contract failed:\n" + "\n".join(errors))

    print("[public_api] surface manifest passed")
    return 0
