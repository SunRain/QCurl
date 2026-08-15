"""Validate QCurl's canonical public Qt metatype inventory."""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any


NAMED_REGISTRATION_PATTERN = re.compile(
    r'qRegisterMetaType\s*<\s*(?P<type>[A-Za-z_][A-Za-z0-9_:]*)\s*>\s*'
    r'\(\s*"(?P<name>[^"]+)"\s*\)',
    flags=re.DOTALL,
)


def _load_inventory(path: Path, errors: list[str]) -> dict[str, Any]:
    try:
        inventory = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"{path}: cannot load JSON: {exc}")
        return {}
    if not isinstance(inventory, dict):
        errors.append(f"{path}: root must be an object")
        return {}
    return inventory


def _qualified_type(type_name: str) -> str:
    if type_name.startswith("QCurl::"):
        return type_name
    return f"QCurl::{type_name}"


def _named_registrations(source_path: Path, errors: list[str]) -> set[tuple[str, str]]:
    try:
        source = source_path.read_text(encoding="utf-8")
    except OSError as exc:
        errors.append(f"cannot read metatype registration source {source_path}: {exc}")
        return set()
    return {
        (_qualified_type(match.group("type")), match.group("name"))
        for match in NAMED_REGISTRATION_PATTERN.finditer(source)
    }


def _registration_map(
    source_root: Path,
    registration_sources: list[str],
    errors: list[str],
) -> dict[str, set[tuple[str, str]]]:
    return {
        relative: _named_registrations(source_root / relative, errors)
        for relative in registration_sources
    }


def _read_consumer_sources(
    consumer_source_dir: Path,
    errors: list[str],
) -> tuple[str, set[Path]]:
    paths = set(consumer_source_dir.glob("*.cpp"))
    try:
        source = "\n".join(
            path.read_text(encoding="utf-8") for path in sorted(paths)
        )
    except OSError as exc:
        errors.append(f"cannot read metatype consumer source: {exc}")
        return "", paths
    if not source:
        errors.append(f"missing metatype consumer source: {consumer_source_dir}")
    return source, paths


def _validate_consumers(
    type_name: str,
    consumers: object,
    consumer_paths: set[Path],
    errors: list[str],
) -> None:
    if not isinstance(consumers, list) or not consumers or not all(
        isinstance(item, str) and item for item in consumers
    ):
        errors.append(f"{type_name}: consumers must be a non-empty string array")
        return
    available = {path.as_posix() for path in consumer_paths}
    for consumer in consumers:
        if not any(path.endswith(consumer) for path in available):
            errors.append(f"{type_name}: consumer source does not exist: {consumer}")


def _validate_entry(
    entry: object,
    registration_sources: list[str],
    registrations_by_source: dict[str, set[tuple[str, str]]],
    consumer_source: str,
    consumer_paths: set[Path],
    errors: list[str],
) -> tuple[str, str] | None:
    if not isinstance(entry, dict):
        errors.append("metatype inventory entry must be an object")
        return None
    type_name = entry.get("type")
    canonical_name = entry.get("canonicalName")
    if not isinstance(type_name, str) or not type_name:
        errors.append("metatype inventory type must be a non-empty string")
        return None
    if not isinstance(canonical_name, str) or not canonical_name:
        errors.append(f"{type_name}: canonicalName must be a non-empty string")
        return None

    reason = entry.get("compatibilityReason")
    if not isinstance(reason, str) or not reason.strip():
        errors.append(f"{type_name}: compatibilityReason must be a non-empty string")
    registration_source = entry.get("registrationSource")
    if registration_source not in registration_sources:
        errors.append(f"{type_name}: unknown registrationSource: {registration_source}")
    pair = (type_name, canonical_name)
    if pair not in registrations_by_source.get(str(registration_source), set()):
        errors.append(f"{type_name}: registrationSource does not register {canonical_name}")

    _validate_consumers(type_name, entry.get("consumers"), consumer_paths, errors)
    if f'"{canonical_name}"' not in consumer_source:
        errors.append(f"{type_name}: canonical name is not consumed by staged smoke")
    if f"QMetaType::fromType<{type_name}>()" not in consumer_source:
        errors.append(f"{type_name}: typed metatype is not consumed by staged smoke")
    return pair


def validate_metatype_inventory(
    inventory_path: Path,
    source_root: Path,
    consumer_source_dir: Path,
) -> list[str]:
    """Return every canonical-name inventory error; an empty list means complete."""

    errors: list[str] = []
    inventory = _load_inventory(inventory_path, errors)
    if errors:
        return errors
    if inventory.get("schemaVersion") != 1:
        errors.append("metatype inventory schemaVersion must be 1")

    registration_sources = inventory.get("registrationSources")
    if not isinstance(registration_sources, list) or not registration_sources or not all(
        isinstance(item, str) and item for item in registration_sources
    ):
        return errors + ["registrationSources must be a non-empty string array"]

    registrations_by_source = _registration_map(
        source_root,
        registration_sources,
        errors,
    )
    actual_registrations = set().union(*registrations_by_source.values())
    consumer_source, consumer_paths = _read_consumer_sources(
        consumer_source_dir,
        errors,
    )

    entries = inventory.get("canonicalNames")
    if not isinstance(entries, list) or not entries:
        return errors + ["canonicalNames must be a non-empty array"]

    inventoried: set[tuple[str, str]] = set()
    seen_types: set[str] = set()
    seen_names: set[str] = set()
    for entry in entries:
        pair = _validate_entry(
            entry,
            registration_sources,
            registrations_by_source,
            consumer_source,
            consumer_paths,
            errors,
        )
        if pair is None:
            continue
        type_name, canonical_name = pair
        if type_name in seen_types:
            errors.append(f"duplicate metatype type: {type_name}")
        if canonical_name in seen_names:
            errors.append(f"duplicate canonical metatype name: {canonical_name}")
        seen_types.add(type_name)
        seen_names.add(canonical_name)
        inventoried.add(pair)

    missing = sorted(actual_registrations - inventoried)
    if missing:
        errors.append(
            "metatype inventory misses named registrations: "
            + ", ".join(f"{type_name}={name}" for type_name, name in missing)
        )
    stale = sorted(inventoried - actual_registrations)
    if stale:
        errors.append(
            "metatype inventory contains stale registrations: "
            + ", ".join(f"{type_name}={name}" for type_name, name in stale)
        )
    return errors
