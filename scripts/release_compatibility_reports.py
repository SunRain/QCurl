"""复核 patch 比较报告引用的输入、旧 ELF、实际加载记录及源码结果。"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

if __package__:
    from .release_identity import file_sha256
    from .qcurl_compatibility import reference_file_entries, validate_reference
    from .qcurl_compatibility_common import (
        candidate_file_entries,
        installed_libraries,
        producer_tuple,
    )
    from .qcurl_compatibility_consumers import (
        _loaded_paths,
        _runtime_inputs,
        _validate_loader,
    )
    from .qcurl_abi_compare import abidiff_command
else:
    from release_identity import file_sha256
    from qcurl_compatibility import reference_file_entries, validate_reference
    from qcurl_compatibility_common import (
        candidate_file_entries,
        installed_libraries,
        producer_tuple,
    )
    from qcurl_compatibility_consumers import (
        _loaded_paths,
        _runtime_inputs,
        _validate_loader,
    )
    from qcurl_abi_compare import abidiff_command


def _read(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"invalid compatibility JSON: {path}")
    return value


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def _verify_runtime(
    results: dict[str, Any],
    reference: dict[str, Any],
    binary_root: Path,
    stage: Path,
    version: str,
    log_root: Path,
) -> None:
    _require(
        set(results) == set(reference["consumers"]),
        "four compatibility consumer results are required",
    )
    for component, result in results.items():
        binary = binary_root / reference["consumers"][component]["binary"]
        _require(
            result.get("result") == "pass" and result.get("returncode") == 0,
            f"compatibility consumer did not pass: {component}",
        )
        _require(
            result.get("binary") == str(binary)
            and result.get("binarySha256") == file_sha256(binary),
            f"compatibility consumer binary changed: {component}",
        )
        loader = log_root / f"{component}.loader.log"
        _require(
            result.get("loaderLog") == str(loader)
            and result.get("loaderSha256") == file_sha256(loader),
            f"compatibility loader trace changed: {component}",
        )
        paths = _loaded_paths(loader.read_text(encoding="utf-8"))
        _validate_loader(paths, stage, version, component)
        _require(
            result.get("loadedLibraries")
            == {name: str(path) for name, path in sorted(paths.items())},
            f"compatibility actual loader paths differ: {component}",
        )
        _require(
            result.get("dependencyInputs") == _runtime_inputs(paths),
            "runtime dependency input changed",
        )
        if component == "TestSupport":
            _require(
                result.get("testSupportBridge") is True,
                "TestSupport static bridge was not tested",
            )


def _verify_identity(report: dict, identity: dict) -> None:
    recorded = report.get("identity", {})
    for key in ("head", "tracked_patch", "untracked", "submodules", "authority"):
        _require(
            recorded.get(key) == identity.get(key),
            f"compatibility report candidate identity mismatch: {key}",
        )


def _verify_inputs(report: dict, reference: Path, build: Path, stage: Path) -> None:
    _require(
        report.get("reference") == str(reference),
        "compatibility reference path mismatch",
    )
    _require(
        report.get("referenceSha256") == file_sha256(reference / "reference.json"),
        "compatibility reference index changed",
    )
    _require(
        report.get("referenceInputs") == reference_file_entries(reference),
        "compatibility reference inputs changed",
    )
    _require(
        report.get("candidateInputs") == candidate_file_entries(build, stage),
        "compatibility candidate inputs changed",
    )
    _require(
        report.get("stagePrefix") == str(stage)
        and report.get("buildDir") == str(build),
        "compatibility producer path mismatch",
    )


def _verify_abi(
    report: dict, output: Path, reference_dir: Path, reference: dict, build: Path
) -> None:
    _require(
        set(report.get("abi", {})) == {"Core", "OtherExtras"},
        "two ABI comparison results are required",
    )
    for component, slug in (("Core", "core"), ("OtherExtras", "other-extras")):
        result = _read(output / "abi" / f"{slug}.comparison.json")
        _require(
            result == report["abi"][component],
            "ABI check and comparison reports differ",
        )
        baseline = reference_dir / reference["libraries"][component]["snapshot"]
        old_library = reference_dir / reference["libraries"][component]["library"]
        new_library = installed_libraries(
            build / "evidence/package-stage/shared", report["version"]
        )[component]
        current = output / "abi" / f"{slug}.snapshot.xml"
        captured = (
            build / "evidence/compatibility/reference/abi" / f"{slug}.snapshot.xml"
        )
        _require(
            file_sha256(current) == file_sha256(captured),
            "ABI comparison snapshot differs from captured candidate",
        )
        old_headers, new_headers = (
            reference_dir / "install/include/qcurl",
            build / "evidence/package-stage/shared/include/qcurl",
        )
        _require(
            result.get("component") == slug
            and result.get("baseline") == str(baseline)
            and result.get("current") == str(current),
            "ABI comparison component or snapshot mismatch",
        )
        _require(
            result.get("baselineLibrary") == str(old_library)
            and result.get("currentLibrary") == str(new_library),
            "ABI comparison ELF provenance mismatch",
        )
        _require(
            result.get("oldHeaders") == str(old_headers)
            and result.get("newHeaders") == str(new_headers),
            "ABI comparison header provenance mismatch",
        )
        _verify_abi_commands(
            result, output, slug, old_library, new_library, old_headers, new_headers
        )


def _verify_abi_commands(
    result: dict,
    output: Path,
    slug: str,
    old_library: Path,
    new_library: Path,
    old_headers: Path,
    new_headers: Path,
) -> None:
    for key, residual in (("raw", False), ("residual", True)):
        command = abidiff_command(
            old_library,
            new_library,
            old_headers,
            new_headers,
            no_added_symbols=residual,
        )
        _require(
            result.get(key, {}).get("command") == command,
            "ABI comparison command provenance mismatch",
        )
        suffix = "residual" if residual else "full"
        _require(
            result.get(key, {}).get("report")
            == str(output / "abi" / f"{slug}.{suffix}.txt"),
            "ABI comparison report path mismatch",
        )


def _verify_check(
    output: Path, reference_dir: Path, build: Path, manifest: dict, *, historical: bool
) -> tuple[dict, dict]:
    reference = validate_reference(reference_dir)
    stage = build / "evidence/package-stage/shared"
    candidate = validate_reference(build / "evidence/compatibility/reference")
    abi_tuple, _ = producer_tuple(build, stage)
    report = _read(output / "report.json")
    classification = "PATCH_COMPATIBLE" if historical else "REFERENCE_VALIDATED"
    _require(
        report.get("schema") == "qcurl-compatibility-check@v1"
        and report.get("result") == "pass"
        and report.get("reasons") == []
        and report.get("classification") == classification
        and report.get("patchCompatibility") is historical,
        "old-binary/ABI check classification mismatch",
    )
    _verify_inputs(report, reference_dir, build, stage)
    _verify_identity(report, manifest["identity"])
    _require(
        report.get("sourceRebuild") == "not-run-separate-operation",
        "old binaries were not kept separate from source rebuild",
    )
    _require(
        report.get("tuple") == abi_tuple == reference["tuple"], "ABI tuple changed"
    )
    _require(
        report.get("version") == candidate["version"]
        and report.get("line") == candidate["line"],
        "compatibility report version mismatch",
    )
    _verify_runtime(
        report.get("referenceSelfTest", {}),
        reference,
        reference_dir,
        reference_dir / "install",
        reference["version"],
        output / "logs/reference-selftest",
    )
    _verify_runtime(
        report.get("candidateRun", {}),
        reference,
        reference_dir,
        stage,
        candidate["version"],
        output / "logs/candidate",
    )
    _verify_abi(report, output, reference_dir, reference, build)
    return reference, candidate


def verify_reference_roundtrip(root: Path, build: Path, manifest: dict) -> None:
    """复核首版参考的同候选 round-trip，绝不作为 patch 兼容结果。"""

    _verify_check(
        root / "roundtrip", root / "reference", build, manifest, historical=False
    )


def verify_historical_outputs(
    root: Path, role: str, reference_dir: Path, build: Path, manifest: dict
) -> None:
    """复算 check/source 的实际引用；不运行旧程序或编译器。"""

    reference, candidate = _verify_check(
        root / f"check-{role}", reference_dir, build, manifest, historical=True
    )
    stage = build / "evidence/package-stage/shared"
    _verify_source(
        root / f"source-{role}",
        reference_dir,
        reference,
        build,
        stage,
        candidate,
        manifest,
    )


def _verify_source(
    output: Path,
    reference_dir: Path,
    reference: dict,
    build: Path,
    stage: Path,
    candidate: dict,
    manifest: dict,
) -> None:
    report = _read(output / "report.json")
    _require(
        report.get("schema") == "qcurl-source-compatibility@v1"
        and report.get("result") == "pass"
        and report.get("reasons") == []
        and report.get("sourceRebuilt") is True
        and report.get("oldBinaryCompatibility") == "not-evaluated",
        "old-source rebuild did not pass",
    )
    _verify_inputs(report, reference_dir, build, stage)
    _verify_identity(report, manifest["identity"])
    wanted = [
        entry
        for entry in reference_file_entries(reference_dir)
        if entry.get("relativePath", "").startswith("sources/")
    ]
    _require(
        report.get("sourceInputs") == wanted, "retained consumer source inputs changed"
    )
    _verify_runtime(
        report.get("consumers", {}),
        reference,
        output,
        stage,
        candidate["version"],
        output / "logs/runtime",
    )
