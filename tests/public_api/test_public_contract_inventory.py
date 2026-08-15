"""Tests for the installed public contract inventory."""

from __future__ import annotations

import json
from pathlib import Path

from tests.public_api.public_contract_inventory import validate_public_contract_inventory


REPO_ROOT = Path(__file__).resolve().parents[2]


def test_public_contract_inventory_covers_installed_surface() -> None:
    """Every installed header must have a completed four-axis contract review."""

    errors = validate_public_contract_inventory(
        REPO_ROOT / "tests" / "public_api" / "public_contract_inventory.json",
        REPO_ROOT / "tests" / "public_api" / "surface_manifest.json",
        REPO_ROOT / "src",
    )

    assert errors == []


def test_public_contract_inventory_rejects_stale_remediated_contracts(tmp_path: Path) -> None:
    """Remediated contracts must not regress to their pre-hard-break descriptions."""

    inventory_path = REPO_ROOT / "tests" / "public_api" / "public_contract_inventory.json"
    inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    stale_contracts = {
        "fallible.connection-pool-manager": "非法配置和未执行 close 仅写日志并返回 void。",
        "fallible.websocket-commands": "void 或 -1 表达同步拒绝，部分非法 payload 被截断。",
    }
    for contract in inventory["contracts"]:
        stale = stale_contracts.get(contract["id"])
        if stale is not None:
            contract["currentContract"] = stale

    stale_inventory_path = tmp_path / "public_contract_inventory.json"
    stale_inventory_path.write_text(
        json.dumps(inventory, ensure_ascii=False),
        encoding="utf-8",
    )

    errors = validate_public_contract_inventory(
        stale_inventory_path,
        REPO_ROOT / "tests" / "public_api" / "surface_manifest.json",
        REPO_ROOT / "src",
    )

    assert any("fallible.connection-pool-manager" in error for error in errors)
    assert any("fallible.websocket-commands" in error for error in errors)
