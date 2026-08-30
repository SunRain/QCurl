"""Validate the QCurl public surface manifest consumed by public-api gates."""

from __future__ import annotations

import json
from argparse import Namespace
from pathlib import Path
from typing import Any
from typing import Callable


FailFunc = Callable[[str], int]

ALLOWED_COMPONENTS = {"Core", "BlockingExtras", "OtherExtras", "TestSupport"}
ALLOWED_MATURITY = {"Stable", "Preview"}
ALLOWED_VISIBILITY = {"Public", "Internal"}
EXPECTED_COMPONENTS = {
    "Core": {
        "cmakeTarget": "QCurl::QCurl",
        "artifact": "runtime-library",
        "defaultConsumer": True,
    },
    "BlockingExtras": {
        "cmakeTarget": "QCurl::BlockingExtras",
        "artifact": "interface-consumer-surface",
        "defaultConsumer": False,
    },
    "OtherExtras": {
        "cmakeTarget": "QCurl::OtherExtras",
        "artifact": "runtime-library",
        "defaultConsumer": False,
    },
    "TestSupport": {
        "cmakeTarget": "QCurl::TestSupport",
        "artifact": "development-static-library",
        "defaultConsumer": False,
    },
}

ALLOWED_INSTALLS = {
    "core-component",
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
        component = entry.get("component")
        maturity = entry.get("maturity")
        visibility = entry.get("visibility")
        current_install = entry.get("currentInstall", "none")
        target_install = entry.get("targetInstall", "none")

        if component not in ALLOWED_COMPONENTS:
            errors.append(f"{path}: invalid component {component!r}")
        if maturity not in ALLOWED_MATURITY:
            errors.append(f"{path}: invalid maturity {maturity!r}")
        if visibility not in ALLOWED_VISIBILITY:
            errors.append(f"{path}: invalid visibility {visibility!r}")
        if current_install not in ALLOWED_INSTALLS:
            errors.append(f"{path}: invalid currentInstall {current_install!r}")
        if target_install not in ALLOWED_INSTALLS:
            errors.append(f"{path}: invalid targetInstall {target_install!r}")
        if visibility == "Internal" and current_install != "none":
            errors.append(f"{path}: Internal header must not have a public install surface")
        if component != "Core" and target_install == "core-component":
            errors.append(f"{path}: non-Core layer must not target the Core component install")


def _validate_schema(data: dict[str, Any], errors: list[str]) -> None:
    if data.get("schemaVersion") != 2:
        errors.append("schemaVersion must be 2")
    if data.get("components") != EXPECTED_COMPONENTS:
        errors.append("components must declare the four QCurl delivery targets")
    if set(data.get("maturityLevels", [])) != ALLOWED_MATURITY:
        errors.append("maturityLevels must list Stable and Preview")
    if set(data.get("visibilityLevels", [])) != ALLOWED_VISIBILITY:
        errors.append("visibilityLevels must list Public and Internal")
    if data.get("compatibilityContract") != {
        "source": "2.x-compatible",
        "abi": "unstable-rebuild-required",
    }:
        errors.append("compatibilityContract must require downstream rebuilds for 2.x updates")


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

        if entry.get("currentInstall") != "core-component":
            errors.append(f"{header}: currentInstall must match generated Core manifest")
        if entry.get("visibility") == "Internal":
            errors.append(f"{header}: Internal header leaked into generated Core manifest")
        if entry.get("component") != "Core":
            errors.append(f"{header}: non-Core header leaked into generated Core manifest")


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

        if entry.get("component") not in {"OtherExtras", "BlockingExtras", "TestSupport"}:
            errors.append(f"{header}: generated extras manifest must not contain Core/Internal layer")
        if entry.get("targetInstall") == "core-component":
            errors.append(f"{header}: extras manifest entry must not target the Core component install")

        expected_install_by_component = {
            "BlockingExtras": "blocking-extras",
            "TestSupport": "test-support",
        }
        expected_install = expected_install_by_component.get(entry.get("component"))
        if expected_install and entry.get("currentInstall") != expected_install:
            errors.append(f"{header}: currentInstall must be {expected_install} for generated extras manifest")


def _validate_blocking_extras_defined(
    data: dict[str, Any],
    entries: dict[str, dict[str, Any]],
    errors: list[str],
) -> None:
    has_installed_blocking_extras = any(
        entry.get("component") == "BlockingExtras"
        and entry.get("targetInstall") == "blocking-extras"
        for entry in entries.values()
    )

    planned = data.get("plannedHeaders", [])
    if not isinstance(planned, list):
        errors.append("plannedHeaders must be an array when present")
        return

    has_planned_blocking_extras = any(
        isinstance(entry, dict) and entry.get("component") == "BlockingExtras" for entry in planned
    )
    if not has_installed_blocking_extras and not has_planned_blocking_extras:
        errors.append("surface manifest must include installed or planned Blocking Extras headers")


def _validate_manifest_completeness(
    core_headers: set[str],
    extras_headers: set[str],
    entries: dict[str, dict[str, Any]],
    errors: list[str],
) -> None:
    overlap = core_headers & extras_headers
    for header in sorted(overlap):
        errors.append(f"{header}: generated Core and Extras manifests overlap")

    for path, entry in entries.items():
        if entry.get("visibility") != "Public" or entry.get("maturity") != "Stable":
            continue
        current_install = entry.get("currentInstall", "none")
        if current_install == "none":
            continue
        generated_headers = core_headers if entry.get("component") == "Core" else extras_headers
        if path not in generated_headers:
            errors.append(
                f"{path}: stable public header is absent from generated install manifests"
            )


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
        target_component = item.get("targetComponent")
        if from_header not in entries:
            errors.append(f"symbolExtractions[{index}].fromHeader must reference a manifest header")
        if not isinstance(symbols, list) or not all(isinstance(symbol, str) for symbol in symbols):
            errors.append(f"symbolExtractions[{index}].symbols must be a string array")
        if target_component not in ALLOWED_COMPONENTS - {"Core"}:
            errors.append(
                f"symbolExtractions[{index}].targetComponent must be a public non-Core component"
            )
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
    _validate_manifest_completeness(core_headers, extras_headers, entries, errors)
    _validate_symbol_extractions(data, entries, errors)

    if errors:
        return fail_func("surface manifest contract failed:\n" + "\n".join(errors))

    print("[public_api] surface manifest passed")
    return 0
