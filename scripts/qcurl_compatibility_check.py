"""Keep ABI comparison, retained ELF execution and source rebuilds separate."""

from __future__ import annotations

import re
from pathlib import Path
from typing import Any

if __package__:
    from .qcurl_compatibility_common import CHECK_SCHEMA, CompatibilityError, LIBRARIES
    from .qcurl_compatibility_common import (
        header_version,
        installed_libraries,
        inventory,
        load_json,
        new_output,
    )
    from .qcurl_compatibility_common import (
        candidate_file_entries,
        producer_tuple,
        read_profile,
        version_parts,
        write_json,
    )
    from .qcurl_compatibility_consumers import build_consumers, run_consumers
    from .qcurl_compatibility_reference import (
        source_identity,
        reference_file_entries,
        validate_reference,
    )
    from .release_identity import file_sha256
else:
    from qcurl_compatibility_common import CHECK_SCHEMA, CompatibilityError, LIBRARIES
    from qcurl_compatibility_common import (
        header_version,
        installed_libraries,
        inventory,
        load_json,
        new_output,
    )
    from qcurl_compatibility_common import (
        candidate_file_entries,
        producer_tuple,
        read_profile,
        version_parts,
        write_json,
    )
    from qcurl_compatibility_consumers import build_consumers, run_consumers
    from qcurl_compatibility_reference import (
        source_identity,
        reference_file_entries,
        validate_reference,
    )
    from release_identity import file_sha256


def _candidate(
    reference: dict[str, Any],
    stage: Path,
    build: Path,
    identity: dict[str, Any],
    *,
    require_history: bool,
) -> tuple[str, dict[str, str]]:
    abi_tuple, profile = producer_tuple(build, stage)
    version = header_version(stage)
    previous, current = version_parts(reference["version"]), version_parts(version)
    if current[:2] != previous[:2]:
        raise CompatibilityError(
            "candidate and reference belong to different compatibility lines"
        )
    if current < previous:
        raise CompatibilityError("candidate is older than the retained reference")
    if abi_tuple != reference["tuple"]:
        raise CompatibilityError("candidate and reference have different ABI tuples")
    if current == previous:
        if require_history:
            raise CompatibilityError(
                "current candidate cannot serve as its own historical reference"
            )
        return "REFERENCE_VALIDATED", profile
    if identity.get("head") == reference["identity"].get("head"):
        raise CompatibilityError(
            "patch reference must come from a different source candidate"
        )
    if require_history:
        _historical_identity(reference["identity"])
    return "PATCH_COMPATIBLE", profile


def _historical_identity(identity: dict[str, Any]) -> None:
    if not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", str(identity.get("head", ""))):
        raise CompatibilityError(
            "historical reference must record a full source commit"
        )
    for section in ("tracked_patch", "untracked", "submodules"):
        record = identity.get(section)
        entries = record.get("entries") if isinstance(record, dict) else None
        if not isinstance(entries, list):
            raise CompatibilityError(
                f"historical reference lacks source identity: {section}"
            )
        if entries and (
            section != "submodules"
            or any(entry.get("dirty", True) for entry in entries)
        ):
            raise CompatibilityError(
                f"historical reference source is not clean: {section}"
            )


def _compare_abis(
    reference_dir: Path,
    reference: dict[str, Any],
    stage: Path,
    output: Path,
    repo: Path,
) -> dict[str, Any]:
    if __package__:
        from .qcurl_abi_compare import compare_snapshots, snapshot_library
    else:
        from qcurl_abi_compare import compare_snapshots, snapshot_library
    result = {}
    for component, library in installed_libraries(stage, header_version(stage)).items():
        slug = LIBRARIES[component][0]
        snapshot = output / "abi" / f"{slug}.snapshot.xml"
        snapshot_library(
            library,
            stage / "include/qcurl",
            snapshot,
            component=slug,
            surface_manifest=repo / "tests/public_api/surface_manifest.json",
            source_root=repo / "src",
            symbol_report=output / "abi" / f"{slug}.dynamic-symbols.json",
        )
        result[component] = compare_snapshots(
            reference_dir / reference["libraries"][component]["snapshot"],
            snapshot,
            reference_dir / "install/include/qcurl",
            stage / "include/qcurl",
            component=slug,
            baseline_library=reference_dir
            / reference["libraries"][component]["library"],
            current_library=library,
            raw_report=output / "abi" / f"{slug}.full.txt",
            residual_report=output / "abi" / f"{slug}.residual.txt",
            result_report=output / "abi" / f"{slug}.comparison.json",
        )
    return result


def _same_version_bytes(
    reference: dict[str, Any], reference_dir: Path, stage: Path
) -> None:
    if inventory(reference_dir / "install", ("include", "lib")) != inventory(
        stage, ("include", "lib")
    ):
        raise CompatibilityError(
            "same-version round-trip requires the identical captured candidate inputs"
        )


def _base_report(reference_dir: Path, stage: Path, build: Path) -> dict[str, Any]:
    return {
        "schema": CHECK_SCHEMA,
        "reference": str(reference_dir),
        "stagePrefix": str(stage),
        "buildDir": str(build),
        "result": "fail",
        "reasons": [],
        "referenceSha256": (
            file_sha256(reference_dir / "reference.json")
            if (reference_dir / "reference.json").is_file()
            else None
        ),
        "referenceSelfTest": {},
        "candidateRun": {},
        "abi": {},
        "sourceRebuild": "not-run-separate-operation",
    }


def check_reference(
    reference_dir: Path,
    stage_prefix: Path,
    output_dir: Path,
    *,
    build_dir: Path,
    repo_root: Path | None = None,
    require_history: bool = False,
    candidate_identity: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Compare ABI and run unchanged reference ELFs; never configure or compile anything."""
    root, stage, build = (
        reference_dir.resolve(),
        stage_prefix.resolve(),
        build_dir.resolve(),
    )
    repo = (repo_root or Path(__file__).resolve().parents[1]).resolve()
    output = new_output(output_dir)
    report = _base_report(root, stage, build)
    try:
        reference = validate_reference(root)
        identity = (
            candidate_identity
            if candidate_identity is not None
            else source_identity(repo)
        )
        classification, profile = _candidate(
            reference, stage, build, identity, require_history=require_history
        )
        report.update(
            version=header_version(stage),
            line=reference["line"],
            tuple=reference["tuple"],
            identity=identity,
            referenceInputs=reference_file_entries(root),
            candidateInputs=candidate_file_entries(build, stage),
        )
        _execute_checks(
            root, reference, stage, output, repo, profile, classification, report
        )
        validate_reference(root)
        report.update(
            result="pass",
            classification=classification,
            patchCompatibility=(classification == "PATCH_COMPATIBLE"),
        )
    except Exception as exc:
        report["reasons"].append(str(exc))
        write_json(output / "report.json", report)
        raise
    write_json(output / "report.json", report)
    return report


def _execute_checks(
    root: Path,
    reference: dict[str, Any],
    stage: Path,
    output: Path,
    repo: Path,
    profile: dict[str, str],
    classification: str,
    report: dict[str, Any],
) -> None:
    captured = load_json(root / reference["validation"])["referenceSelfTest"]
    report["referenceSelfTest"] = run_consumers(
        root,
        reference["consumers"],
        root / "install",
        reference["version"],
        read_profile(root / reference["profile"]),
        output / "logs/reference-selftest",
        expected=captured,
    )
    if classification == "REFERENCE_VALIDATED":
        _same_version_bytes(reference, root, stage)
    report["abi"] = _compare_abis(root, reference, stage, output, repo)
    report["candidateRun"] = run_consumers(
        root,
        reference["consumers"],
        stage,
        header_version(stage),
        profile,
        output / "logs/candidate",
        expected=captured,
    )


def _source_input_metadata(
    root: Path,
    reference: dict[str, Any],
    build: Path,
    stage: Path,
    identity: dict[str, Any],
) -> dict[str, Any]:
    inputs = reference_file_entries(root)
    return {
        "result": "pass",
        "version": header_version(stage),
        "line": reference["line"],
        "identity": identity,
        "referenceInputs": inputs,
        "referenceSha256": file_sha256(root / "reference.json"),
        "sourceInputs": [
            entry
            for entry in inputs
            if entry.get("relativePath", "").startswith("sources/")
        ],
        "candidateInputs": candidate_file_entries(build, stage),
        "sourceRebuilt": True,
        "oldBinaryCompatibility": "not-evaluated",
    }


def rebuild_source(
    reference_dir: Path,
    stage_prefix: Path,
    output_dir: Path,
    *,
    build_dir: Path,
    repo_root: Path | None = None,
    candidate_identity: dict[str, Any] | None = None,
    require_history: bool = False,
) -> dict[str, Any]:
    """Produce separately identified evidence by rebuilding the retained source inputs."""
    root, stage, build = (
        reference_dir.resolve(),
        stage_prefix.resolve(),
        build_dir.resolve(),
    )
    repo = (repo_root or Path(__file__).resolve().parents[1]).resolve()
    output = new_output(output_dir)
    report = {
        "schema": "qcurl-source-compatibility@v1",
        "reference": str(root),
        "stagePrefix": str(stage),
        "buildDir": str(build),
        "result": "fail",
        "reasons": [],
        "consumers": {},
    }
    try:
        reference = validate_reference(root)
        identity = (
            candidate_identity
            if candidate_identity is not None
            else source_identity(repo)
        )
        _, profile = _candidate(
            reference, stage, build, identity, require_history=require_history
        )
        consumers = build_consumers(
            root,
            reference["consumers"],
            build / "generated/consumer-profile.cmake",
            stage,
            output,
        )
        report["consumers"] = run_consumers(
            output,
            consumers,
            stage,
            header_version(stage),
            profile,
            output / "logs/runtime",
        )
        report.update(_source_input_metadata(root, reference, build, stage, identity))
    except Exception as exc:
        report["reasons"].append(str(exc))
        write_json(output / "report.json", report)
        raise
    write_json(output / "report.json", report)
    return report
