"""Planner helpers for the libcurl consistency gate."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from .contract_map import coverage_pytest_files
from .contract_map import load_coverage_map


def pytest_files(cfg: Any) -> list[str]:
    """Return pytest files planned for a gate suite."""
    map_path = Path(cfg.repo_root) / "tests/libcurl_consistency/coverage-map.yaml"
    return coverage_pytest_files(load_coverage_map(map_path), cfg.suite, cfg.with_ext)


def plan_pytest_files(
    cfg: Any,
    capability_manifest: dict[str, object],
    *,
    planner_overrides: dict[str, object] | None = None,
) -> tuple[list[str], dict[str, str]]:
    """根据不可变能力清单和本轮覆盖规则生成 pytest 文件计划。"""

    planned: list[str] = []
    exclusions: dict[str, str] = {}
    tests = capability_manifest.get("tests") if isinstance(capability_manifest, dict) else {}
    tests_map = tests if isinstance(tests, dict) else {}
    overrides = planner_overrides or {}

    for path in pytest_files(cfg):
        rule = tests_map.get(Path(path).name)
        override = overrides.get(Path(path).name)
        enabled = True
        reason = ""
        if isinstance(rule, dict):
            enabled = bool(rule.get("enabled", True))
            reason = str(rule.get("reason") or "")
        if isinstance(override, dict):
            enabled = bool(override.get("enabled", enabled))
            reason = str(override.get("reason") or reason)
        if enabled:
            planned.append(path)
        else:
            exclusions[path] = reason or "disabled by capability manifest"
    return planned, exclusions
