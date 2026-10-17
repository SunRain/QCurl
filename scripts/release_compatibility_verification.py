"""独立复核版本线、参考输入和兼容产物，拒绝自报兼容通过。"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

if __package__:
    from . import release_identity
    from .release_compatibility_policy import reference_input_records
    from .release_compatibility_policy import (
        require_clean_identity,
        validate_abi_arguments,
    )
    from .release_compatibility_reports import (
        verify_historical_outputs as _verify_historical_outputs,
    )
    from .release_compatibility_reports import verify_reference_roundtrip
else:
    import release_identity
    from release_compatibility_policy import reference_input_records
    from release_compatibility_policy import (
        require_clean_identity,
        validate_abi_arguments,
    )
    from release_compatibility_reports import (
        verify_historical_outputs as _verify_historical_outputs,
    )
    from release_compatibility_reports import verify_reference_roundtrip


def _object(path: Path) -> dict[str, Any]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise ValueError(f"compatibility report must be an object: {path}")
    return payload


def _reference_reasons(
    manifest: dict[str, Any],
    build: Path,
) -> list[str]:
    if __package__:
        from .qcurl_compatibility import verify_capture
    else:
        from qcurl_compatibility import verify_capture
    root = build / "evidence" / "compatibility" / "reference"
    expected = manifest.get("identity", {})
    reference = verify_capture(
        root, build, build / "evidence/package-stage/shared", expected_identity=expected
    )
    actual = reference.get("identity", {})
    reasons = [
        f"compatibility reference candidate identity mismatch: {key}"
        for key in ("head", "tracked_patch", "untracked", "submodules")
        if actual.get(key) != expected.get(key)
    ]
    validation = _object(root / "validation.json")
    if validation.get("result") != "REFERENCE_VALIDATED":
        reasons.append("first-release reference was not validated")
    return reasons


def _comparison_reasons(output: Path) -> list[str]:
    reasons = []
    for component in ("core", "other-extras"):
        report = _object(output / "abi" / f"{component}.comparison.json")
        if (
            report.get("schema") != "qcurl-abi-comparison@v1"
            or report.get("compatible") is not True
            or report.get("residual", {}).get("returncode") != 0
            or report.get("raw", {}).get("returncode") not in (0, 4)
        ):
            reasons.append(f"ABI comparison did not pass: {output.name}/{component}")
    return reasons


def compatibility_reasons(
    manifest: dict[str, Any],
    repo: Path,
    registry: dict[str, Any],
    *,
    args: argparse.Namespace,
) -> list[str]:
    """按真实候选/CLI 重算模式和历史输入，并核对参考全量文件。"""

    reasons = []
    args.source_root = repo.resolve()
    try:
        validate_abi_arguments(args, repo)
        if manifest.get("abiMode") != args.abi_mode:
            reasons.append("compatibility mode differs from requested mode")
        if args.stage == "final":
            require_clean_identity(manifest.get("identity", {}))
        expected_inputs = reference_input_records(args, repo)
        if manifest.get("compatibilityInputs", {}) != expected_inputs:
            reasons.append("historical compatibility inputs changed or are missing")
        if args.abi_mode == "none":
            return reasons
        shared = registry.get("release-shared", {})
        build = Path(shared["path"])
        reasons.extend(_reference_reasons(manifest, build))
        root = build / "evidence" / "compatibility"
        reasons.extend(_comparison_reasons(root / "roundtrip"))
        verify_reference_roundtrip(root, build, manifest)
        for role in expected_inputs:
            reasons.extend(_comparison_reasons(root / f"check-{role}"))
            _verify_historical_outputs(
                root, role, Path(expected_inputs[role]["path"]), build, manifest
            )
    except (OSError, ValueError, KeyError, TypeError, RuntimeError) as exc:
        reasons.append(f"compatibility evidence invalid: {exc}")
    return reasons
