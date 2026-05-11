"""
Capability manifest helpers for libcurl_consistency gate.
"""

from __future__ import annotations

import json
import os
from pathlib import Path

import pytest


_REPO_ROOT = Path(__file__).resolve().parents[3]


def _manifest_path() -> Path:
    raw = (os.environ.get("QCURL_LC_CAPABILITY_MANIFEST") or "").strip()
    if raw:
        return Path(raw).expanduser().resolve()
    return (_REPO_ROOT / "build" / "libcurl_consistency" / "reports" / "capabilities.json").resolve()


def load_capability_manifest() -> dict:
    path = _manifest_path()
    if not path.exists():
        pytest.fail(f"capability manifest missing: {path}")
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:  # pragma: no cover - defensive guard
        pytest.fail(f"failed to parse capability manifest {path}: {exc}")
    raise AssertionError("unreachable")


def guard_planned_test(file_name: str) -> dict:
    manifest = load_capability_manifest()
    tests = manifest.get("tests") if isinstance(manifest, dict) else {}
    entry = tests.get(file_name) if isinstance(tests, dict) else None
    if isinstance(entry, dict) and not bool(entry.get("enabled", True)):
        reason = str(entry.get("reason") or "disabled by capability manifest")
        pytest.fail(f"gate/planner should have excluded {file_name}: {reason}")
    return manifest
