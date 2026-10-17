"""Synthetic mini-ELF inputs for release-verifier unit tests, never product qualification.

The ELF consumers really execute and retain loader logs, but these fixtures do not
exercise QCurl, Qt, libcurl or the production public consumers.
"""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path
from typing import Any

from scripts.check_release_contract import parse_project_version
from scripts.qcurl_compatibility_common import inventory, load_json, write_json
from scripts.qcurl_compatibility_reference import INPUT_DIRECTORIES, verify_capture
from scripts.qcurl_compatibility_reference import (
    reference_file_entries,
    validate_reference,
)
from scripts.qcurl_compatibility_check import rebuild_source
from scripts.qcurl_compatibility_common import (
    candidate_file_entries,
    installed_libraries,
    producer_tuple,
)
from scripts.qcurl_compatibility_consumers import run_consumers
from scripts.qcurl_abi_compare import abidiff_command, run_abidiff_report
from scripts.release_identity import file_sha256
from tests.compatibility.fixture import make_reference_fixture


def _merge_producer_cache(build: Path, fixture_build: Path) -> None:
    cache = build / "CMakeCache.txt"
    lines = cache.read_text(encoding="utf-8").splitlines() if cache.exists() else []
    retained = (
        (fixture_build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines()
    )
    updates = {line.split(":", 1)[0]: line for line in retained if ":" in line}
    merged = []
    for line in lines:
        name = line.split(":", 1)[0] if ":" in line else ""
        merged.append(updates.pop(name, line))
    cache.write_text("\n".join([*merged, *updates.values()]) + "\n", encoding="utf-8")


def _bind_producer(
    reference: Path, build: Path, fixture_build: Path, stage: Path
) -> None:
    (build / "generated").mkdir(parents=True, exist_ok=True)
    profile = build / "generated/consumer-profile.cmake"
    shutil.copy2(fixture_build / "generated/consumer-profile.cmake", profile)
    _merge_producer_cache(build, fixture_build)
    shutil.copy2(build / "CMakeCache.txt", reference / "profile/CMakeCache.txt")
    package = build / "evidence/package/shared-install-consumer.json"
    report = {
        "stagePrefix": str(stage),
        "syntheticToolFixture": True,
        "profile": {"path": str(profile), "sha256": file_sha256(profile)},
    }
    original = load_json(
        fixture_build / "evidence/package/shared-install-consumer.json"
    )
    report["installInventory"] = original["installInventory"]
    write_json(package, report)
    write_json(reference / "profile/package-install-consumer.json", report)
    payload = load_json(reference / "reference.json")
    payload["origin"] = {
        "buildDir": str(build),
        "stagePrefix": str(stage),
        "packageReport": str(package),
        "packageReportSha256": file_sha256(package),
        "profile": str(profile),
        "profileSha256": file_sha256(profile),
    }
    payload["files"] = inventory(reference, INPUT_DIRECTORIES, ("validation.json",))
    write_json(reference / "reference.json", payload)


def prepare_snapshot_fixture(
    repo: Path,
    build: Path,
    *,
    identity: dict[str, Any] | None = None,
) -> Path:
    """Prepare real synthetic ELF capture inputs at the fixed release-evidence paths."""

    repo, build = repo.resolve(), build.resolve()
    version = ".".join(str(value) for value in parse_project_version(repo))
    head = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=repo,
        check=True,
        text=True,
        capture_output=True,
    ).stdout.strip()
    reference, fixture_build, fixture_stage = make_reference_fixture(
        build / "evidence/compatibility",
        version=version,
        head=head,
    )
    stage = build / "evidence/package-stage/shared"
    stage.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(fixture_stage, stage, symlinks=True)
    (build / "src").mkdir(parents=True, exist_ok=True)
    for library in sorted((fixture_build / "src").iterdir()):
        shutil.copy2(library, build / "src" / library.name)
    _bind_producer(reference, build, fixture_build, stage)
    if identity is not None:
        bind_fixture_identity(reference, identity)
    verify_capture(reference, build, stage, expected_identity=identity)
    return reference


def bind_fixture_identity(reference: Path, identity: dict[str, Any]) -> None:
    """Bind the synthetic capture after its test repository identity is finalized."""

    index = reference / "reference.json"
    payload = load_json(index)
    payload["identity"] = identity
    # reference.json is bound by the release artifact digest, not its own inventory.
    write_json(index, payload)


def _comparison_abi(
    reference: Path, candidate: Path, stage: Path, output: Path
) -> dict[str, Any]:
    result = {}
    old_version = load_json(reference / "reference.json")["version"]
    new_version = load_json(candidate / "reference.json")["version"]
    for component, slug in (("Core", "core"), ("OtherExtras", "other-extras")):
        baseline = reference / "abi" / f"{slug}.snapshot.xml"
        current = output / "abi" / f"{slug}.snapshot.xml"
        current.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(candidate / "abi" / f"{slug}.snapshot.xml", current)
        old_library = installed_libraries(reference / "install", old_version)[component]
        new_library = installed_libraries(stage, new_version)[component]
        old_headers, new_headers = (
            reference / "install/include/qcurl",
            stage / "include/qcurl",
        )
        comparison = {
            "schema": "qcurl-abi-comparison@v1",
            "syntheticToolFixture": True,
            "component": slug,
            "baseline": str(baseline),
            "current": str(current),
            "baselineLibrary": str(old_library),
            "currentLibrary": str(new_library),
            "oldHeaders": str(old_headers),
            "newHeaders": str(new_headers),
        }
        for name, residual in (("raw", False), ("residual", True)):
            command = abidiff_command(
                old_library,
                new_library,
                old_headers,
                new_headers,
                no_added_symbols=residual,
            )
            path = output / "abi" / f"{slug}.{'residual' if residual else 'full'}.txt"
            comparison[name] = run_abidiff_report(command, path)
            if comparison[name]["returncode"] != 0:
                raise AssertionError(
                    f"identical synthetic ELF comparison failed: {comparison}"
                )
        comparison["compatible"] = True
        write_json(output / "abi" / f"{slug}.comparison.json", comparison)
        result[component] = comparison
    return result


def _comparison_fixture(
    reference: Path,
    build: Path,
    output: Path,
    identity: dict[str, Any],
    *,
    historical: bool,
) -> None:
    output.mkdir(parents=True)
    stage = build / "evidence/package-stage/shared"
    candidate = build / "evidence/compatibility/reference"
    payload = validate_reference(reference)
    current = validate_reference(candidate)
    abi_tuple, profile = producer_tuple(build, stage)
    expected = load_json(reference / "validation.json")["referenceSelfTest"]
    report = {
        "schema": "qcurl-compatibility-check@v1",
        "syntheticToolFixture": True,
        "result": "pass",
        "reasons": [],
        "classification": "PATCH_COMPATIBLE" if historical else "REFERENCE_VALIDATED",
        "patchCompatibility": historical,
        "sourceRebuild": "not-run-separate-operation",
        "reference": str(reference),
        "referenceSha256": file_sha256(reference / "reference.json"),
        "referenceInputs": reference_file_entries(reference),
        "candidateInputs": candidate_file_entries(build, stage),
        "buildDir": str(build),
        "stagePrefix": str(stage),
        "identity": identity,
        "version": current["version"],
        "line": current["line"],
        "tuple": abi_tuple,
    }
    report["referenceSelfTest"] = run_consumers(
        reference,
        payload["consumers"],
        reference / "install",
        payload["version"],
        profile,
        output / "logs/reference-selftest",
        expected=expected,
    )
    report["candidateRun"] = run_consumers(
        reference,
        payload["consumers"],
        stage,
        current["version"],
        profile,
        output / "logs/candidate",
        expected=expected,
    )
    report["abi"] = _comparison_abi(reference, candidate, stage, output)
    write_json(output / "report.json", report)


def prepare_roundtrip_fixture(build: Path, identity: dict[str, Any]) -> None:
    """Assemble verifier inputs from actual synthetic ELF runs, not QCurl qualification."""

    root = build / "evidence/compatibility"
    _comparison_fixture(
        root / "reference", build, root / "roundtrip", identity, historical=False
    )


def prepare_historical_fixture(
    repo: Path, build: Path, references: tuple[Path, Path], identity: dict[str, Any]
) -> None:
    """Run synthetic old ELFs and independently rebuild their retained source inputs."""

    root = build / "evidence/compatibility"
    for role, reference in zip(("first", "latest"), references):
        if role == "latest" and reference == references[0]:
            continue
        _comparison_fixture(
            reference, build, root / f"check-{role}", identity, historical=True
        )
        rebuild_source(
            reference,
            build / "evidence/package-stage/shared",
            root / f"source-{role}",
            build_dir=build,
            repo_root=repo,
            candidate_identity=identity,
            require_history=True,
        )
