"""Tests for the canonical public metatype inventory."""

from __future__ import annotations

import json

from pathlib import Path

from tests.public_api.metatype_inventory import validate_metatype_inventory


REPO_ROOT = Path(__file__).resolve().parents[2]


def test_metatype_inventory_covers_named_registrations_and_consumers() -> None:
    """Every named registration must map to a real staged consumer."""

    errors = validate_metatype_inventory(
        REPO_ROOT / "tests" / "public_api" / "metatype_inventory.json",
        REPO_ROOT / "src",
        REPO_ROOT / "tests" / "public_api" / "consumer_metatype_smoke",
    )

    assert errors == []


def test_metatype_inventory_rejects_omitted_named_registration(tmp_path: Path) -> None:
    """A source registration omitted from the inventory must fail validation."""

    inventory_path = REPO_ROOT / "tests" / "public_api" / "metatype_inventory.json"
    inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    inventory["canonicalNames"] = inventory["canonicalNames"][:-1]
    incomplete_path = tmp_path / "metatype_inventory.json"
    incomplete_path.write_text(json.dumps(inventory, ensure_ascii=False), encoding="utf-8")

    errors = validate_metatype_inventory(
        incomplete_path,
        REPO_ROOT / "src",
        REPO_ROOT / "tests" / "public_api" / "consumer_metatype_smoke",
    )

    assert any("misses named registrations" in error for error in errors)


def test_metatype_inventory_rejects_omitted_unnamed_registration(tmp_path: Path) -> None:
    """无参注册也必须被 inventory 和实际安装消费者覆盖。"""
    inventory = json.loads(
        (REPO_ROOT / "tests/public_api/metatype_inventory.json").read_text(encoding="utf-8")
    )
    inventory["canonicalNames"] = [
        entry for entry in inventory["canonicalNames"]
        if entry["type"] != "QCurl::SchedulerCommandResult"
    ]
    path = tmp_path / "metatype_inventory.json"
    path.write_text(json.dumps(inventory, ensure_ascii=False), encoding="utf-8")
    errors = validate_metatype_inventory(
        path, REPO_ROOT / "src", REPO_ROOT / "tests/public_api/consumer_metatype_smoke"
    )
    assert any("QCurl::SchedulerCommandResult=" in error for error in errors)


def test_metatype_inventory_rejects_misattributed_registration_source(
    tmp_path: Path,
) -> None:
    """Each canonical name must point to the file that performs registration."""

    inventory_path = REPO_ROOT / "tests" / "public_api" / "metatype_inventory.json"
    inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    inventory["canonicalNames"][0]["registrationSource"] = "QCNetworkRequestPriority.h"
    invalid_path = tmp_path / "metatype_inventory.json"
    invalid_path.write_text(json.dumps(inventory, ensure_ascii=False), encoding="utf-8")

    errors = validate_metatype_inventory(
        invalid_path,
        REPO_ROOT / "src",
        REPO_ROOT / "tests" / "public_api" / "consumer_metatype_smoke",
    )

    assert any("registrationSource does not register" in error for error in errors)


def test_metatype_inventory_rejects_missing_consumer(tmp_path: Path) -> None:
    """A declared compatibility consumer must resolve to an exercised fixture source."""

    inventory_path = REPO_ROOT / "tests" / "public_api" / "metatype_inventory.json"
    inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    inventory["canonicalNames"][0]["consumers"] = [
        "tests/public_api/consumer_metatype_smoke/missing.cpp"
    ]
    invalid_path = tmp_path / "metatype_inventory.json"
    invalid_path.write_text(json.dumps(inventory, ensure_ascii=False), encoding="utf-8")

    errors = validate_metatype_inventory(
        invalid_path,
        REPO_ROOT / "src",
        REPO_ROOT / "tests" / "public_api" / "consumer_metatype_smoke",
    )

    assert any("consumer source does not exist" in error for error in errors)
