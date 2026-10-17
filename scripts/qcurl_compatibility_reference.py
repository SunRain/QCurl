"""Capture and independently validate retained patch-compatibility inputs."""

from __future__ import annotations

import re
import shutil
from pathlib import Path
from typing import Any

if __package__:
    from .qcurl_compatibility_common import (
        CompatibilityError,
        LIBRARIES,
        REFERENCE_SCHEMA,
    )
    from .qcurl_compatibility_common import (
        file_entry,
        header_version,
        installed_libraries,
        inventory,
    )
    from .qcurl_compatibility_common import (
        load_json,
        new_output,
        producer_tuple,
        read_profile,
    )
    from .qcurl_compatibility_common import (
        relative_file,
        tuple_for_stage,
        validate_file_entries,
        write_json,
    )
    from .qcurl_compatibility_consumers import (
        build_consumers,
        copy_consumer_sources,
        inspect_binary,
        run_consumers,
    )
    from .release_identity import (
        _authority,
        _run,
        _submodules,
        _tracked_patch,
        _untracked,
    )
    from .release_identity import build_identity, default_authority_paths, file_sha256
    from .release_package_evidence import CONSUMERS
else:
    from qcurl_compatibility_common import (
        CompatibilityError,
        LIBRARIES,
        REFERENCE_SCHEMA,
    )
    from qcurl_compatibility_common import (
        file_entry,
        header_version,
        installed_libraries,
        inventory,
    )
    from qcurl_compatibility_common import (
        load_json,
        new_output,
        producer_tuple,
        read_profile,
    )
    from qcurl_compatibility_common import (
        relative_file,
        tuple_for_stage,
        validate_file_entries,
        write_json,
    )
    from qcurl_compatibility_consumers import (
        build_consumers,
        copy_consumer_sources,
        inspect_binary,
        run_consumers,
    )
    from release_identity import (
        _authority,
        _run,
        _submodules,
        _tracked_patch,
        _untracked,
    )
    from release_identity import build_identity, default_authority_paths, file_sha256
    from release_package_evidence import CONSUMERS

INPUT_DIRECTORIES = ("install", "sources", "bin", "profile", "abi", "logs")


def _identity(repo: Path, build: Path) -> dict[str, Any]:
    return build_identity(
        repo,
        build_dirs=[build],
        authority_paths=default_authority_paths(repo),
        commands=[],
    )


def source_identity(repo: Path) -> dict[str, Any]:
    """Reuse release source identity without invoking compiler or configuration tools."""
    return {
        "head": _run(repo, ["git", "rev-parse", "HEAD"]),
        "tracked_patch": _tracked_patch(repo),
        "untracked": _untracked(repo),
        "submodules": _submodules(repo),
        "authority": _authority(repo, default_authority_paths(repo)),
    }


def _retained_producer(build: Path, output: Path, repo: Path) -> dict[str, Any]:
    profile = output / "profile"
    profile.mkdir()
    for source, name in (
        (build / "generated/consumer-profile.cmake", "consumer-profile.cmake"),
        (build / "CMakeCache.txt", "CMakeCache.txt"),
        (repo / "tests/public_api/surface_manifest.json", "surface-manifest.json"),
    ):
        if not source.is_file():
            raise CompatibilityError(f"required producer input missing: {source}")
        shutil.copy2(source, profile / name)
    report = build / "evidence/package/shared-install-consumer.json"
    if not report.is_file():
        raise CompatibilityError(f"producer package-install report missing: {report}")
    shutil.copy2(report, profile / "package-install-consumer.json")
    return {
        "buildDir": str(build),
        "packageReport": str(report),
        "packageReportSha256": file_sha256(report),
        "profile": str(build / "generated/consumer-profile.cmake"),
        "profileSha256": file_sha256(profile / "consumer-profile.cmake"),
    }


def _capture_snapshots(
    output: Path, repo: Path, libraries: dict[str, Path]
) -> dict[str, Any]:
    if __package__:
        from .qcurl_abi_compare import snapshot_library
    else:
        from qcurl_abi_compare import snapshot_library
    result = {}
    for component, library in libraries.items():
        slug = LIBRARIES[component][0]
        snapshot = output / "abi" / f"{slug}.snapshot.xml"
        symbols = output / "abi" / f"{slug}.dynamic-symbols.json"
        operation = snapshot_library(
            library,
            output / "install/include/qcurl",
            snapshot,
            component=slug,
            surface_manifest=output / "profile/surface-manifest.json",
            source_root=repo / "src",
            symbol_report=symbols,
        )
        write_json(output / "abi" / f"{slug}.snapshot-command.json", operation)
        result[component] = {
            "library": library.relative_to(output).as_posix(),
            "snapshot": snapshot.relative_to(output).as_posix(),
            "symbols": symbols.relative_to(output).as_posix(),
        }
    return result


def capture_reference(
    build_dir: Path,
    stage_prefix: Path,
    output_dir: Path,
    *,
    repo_root: Path | None = None,
    candidate_identity: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Capture a new immutable reference, including a real once-built consumer self-test."""
    build, stage = build_dir.resolve(), stage_prefix.resolve()
    repo = (repo_root or Path(__file__).resolve().parents[1]).resolve()
    abi_tuple, profile = producer_tuple(build, stage)
    if output_dir.resolve().is_relative_to(stage):
        raise CompatibilityError(
            "reference output must not be inside the installed input stage"
        )
    output = new_output(output_dir)
    version = header_version(stage)
    origin = _retained_producer(build, output, repo)
    origin["stagePrefix"] = str(stage)
    shutil.copytree(stage, output / "install", symlinks=True)
    sources = copy_consumer_sources(repo, output)
    consumers = build_consumers(
        output,
        sources,
        output / "profile/consumer-profile.cmake",
        output / "install",
        output,
    )
    libraries = _capture_snapshots(
        output, repo, installed_libraries(output / "install", version)
    )
    _capture_selftest(output, consumers, version, profile)
    payload = {
        "schema": REFERENCE_SCHEMA,
        "version": version,
        "line": ".".join(version.split(".")[:2]),
        "identity": (
            candidate_identity
            if candidate_identity is not None
            else _identity(repo, build)
        ),
        "origin": origin,
        "tuple": abi_tuple,
        "libraries": libraries,
        "consumers": consumers,
        "profile": "profile/consumer-profile.cmake",
        "validation": "validation.json",
    }
    payload["files"] = inventory(output, INPUT_DIRECTORIES, ("validation.json",))
    write_json(output / "reference.json", payload)
    validate_reference(output)
    return payload


def _capture_selftest(
    output: Path, consumers: dict[str, Any], version: str, profile: dict[str, str]
) -> None:
    runtime = run_consumers(
        output,
        consumers,
        output / "install",
        version,
        profile,
        output / "logs/selftest",
    )
    write_json(
        output / "validation.json",
        {
            "schema": "qcurl-reference-validation@v1",
            "result": "REFERENCE_VALIDATED",
            "referenceSelfTest": runtime,
            "sourceRebuild": "captured-once",
            "patchCompatibility": "not-evaluated",
        },
    )


def _validate_structure(root: Path, payload: dict[str, Any]) -> None:
    if payload.get("schema") != REFERENCE_SCHEMA:
        raise CompatibilityError("unsupported compatibility reference schema")
    if (
        payload.get("profile") != "profile/consumer-profile.cmake"
        or payload.get("validation") != "validation.json"
    ):
        raise CompatibilityError("reference profile/validation paths are not canonical")
    if payload.get("version") != header_version(root / "install"):
        raise CompatibilityError(
            "reference version does not match its installed headers"
        )
    if payload.get("line") != ".".join(payload["version"].split(".")[:2]):
        raise CompatibilityError("reference compatibility line is inconsistent")
    if set(payload.get("libraries", {})) != set(LIBRARIES):
        raise CompatibilityError("reference must cover both runtime libraries")
    if set(payload.get("consumers", {})) != {spec.target for spec in CONSUMERS}:
        raise CompatibilityError("reference must cover all four consumers")
    identity = payload.get("identity")
    if (
        not isinstance(identity, dict)
        or not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", str(identity.get("head", "")))
        or not identity.get("submodules")
    ):
        raise CompatibilityError("reference source identity is incomplete")
    profile = read_profile(relative_file(root, payload.get("profile", "")))
    if tuple_for_stage(root / "install", profile) != payload.get("tuple"):
        raise CompatibilityError(
            "reference tuple does not match its retained producer profile"
        )
    _validate_origin(root, payload)


def _validate_origin(root: Path, payload: dict[str, Any]) -> None:
    origin = payload.get("origin")
    if not isinstance(origin, dict):
        raise CompatibilityError("reference lacks producer provenance")
    report = load_json(root / "profile/package-install-consumer.json")
    profile_hash = file_sha256(root / "profile/consumer-profile.cmake")
    if (
        origin.get("profileSha256") != profile_hash
        or origin.get("packageReportSha256")
        != file_sha256(root / "profile/package-install-consumer.json")
        or report.get("stagePrefix") != origin.get("stagePrefix")
        or report.get("profile", {}).get("path") != origin.get("profile")
        or report.get("profile", {}).get("sha256") != profile_hash
    ):
        raise CompatibilityError(
            "reference producer provenance and retained inputs differ"
        )


def _validate_semantics(root: Path, payload: dict[str, Any]) -> None:
    if __package__:
        from .qcurl_abi_compare import validate_component_snapshot
    else:
        from qcurl_abi_compare import validate_component_snapshot
    indexed = {entry["path"] for entry in payload["files"]}
    for component, library in payload["libraries"].items():
        slug, stem = LIBRARIES[component]
        expected = {
            "library": f"install/lib/{stem}.so.{payload['version']}",
            "snapshot": f"abi/{slug}.snapshot.xml",
            "symbols": f"abi/{slug}.dynamic-symbols.json",
        }
        if library != expected or any(
            value not in indexed for value in expected.values()
        ):
            raise CompatibilityError(
                f"reference library index is incomplete: {component}"
            )
        validate_component_snapshot(relative_file(root, library["snapshot"]), slug)
    for spec in CONSUMERS:
        consumer = payload["consumers"][spec.target]
        expected = {
            "source": f"sources/{spec.target}",
            "binary": f"bin/{spec.target}",
            "executable": spec.executable,
        }
        if consumer != expected or consumer["binary"] not in indexed:
            raise CompatibilityError(
                f"reference consumer index is incomplete: {spec.target}"
            )
        inspect_binary(relative_file(root, consumer["binary"]), component=spec.target)
    validation = load_json(relative_file(root, payload.get("validation", "")))
    if (
        validation.get("schema") != "qcurl-reference-validation@v1"
        or validation.get("result") != "REFERENCE_VALIDATED"
        or set(validation.get("referenceSelfTest", {})) != set(payload["consumers"])
    ):
        raise CompatibilityError("reference self-test evidence is incomplete")
    for component, result in validation["referenceSelfTest"].items():
        if result.get("result") != "pass" or result.get("returncode") != 0:
            raise CompatibilityError(f"reference self-test did not pass: {component}")
        if result.get("binarySha256") != file_sha256(
            root / payload["consumers"][component]["binary"]
        ):
            raise CompatibilityError(f"reference self-test binary differs: {component}")


def validate_reference(reference_dir: Path) -> dict[str, Any]:
    """Independently check all retained inputs; never configure, build or run consumers."""
    root = reference_dir.resolve()
    payload = load_json(root / "reference.json")
    validate_file_entries(root, payload.get("files"))
    if payload["files"] != inventory(root, INPUT_DIRECTORIES, ("validation.json",)):
        raise CompatibilityError(
            "reference inventory does not cover its retained inputs"
        )
    _validate_structure(root, payload)
    _validate_semantics(root, payload)
    return payload


def reference_file_entries(reference_dir: Path) -> list[dict[str, Any]]:
    """Return actual reference input paths and digests for the existing release verifier."""
    root = reference_dir.resolve()
    payload = validate_reference(root)
    entries = [file_entry(root, root / "reference.json"), *payload["files"]]
    return [
        {**entry, "relativePath": entry["path"], "path": str(root / entry["path"])}
        for entry in entries
    ]


def verify_capture(
    reference_dir: Path,
    build_dir: Path,
    stage_prefix: Path,
    *,
    expected_identity: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Verify a capture against its producer without executing binaries or a build."""
    root, build, stage = (
        reference_dir.resolve(),
        build_dir.resolve(),
        stage_prefix.resolve(),
    )
    payload = validate_reference(root)
    abi_tuple, _ = producer_tuple(build, stage)
    if payload["tuple"] != abi_tuple or payload["version"] != header_version(stage):
        raise CompatibilityError("capture and producer tuple/version differ")
    retained = inventory(root / "install", ("include", "lib"))
    current = inventory(stage, ("include", "lib"))
    if retained != current:
        raise CompatibilityError(
            "capture installed inputs differ from the current producer stage"
        )
    profile = build / "generated/consumer-profile.cmake"
    if payload["origin"].get("profileSha256") != file_sha256(profile):
        raise CompatibilityError("capture producer profile changed")
    if expected_identity is not None:
        for key in ("head", "tracked_patch", "untracked", "submodules", "authority"):
            if payload["identity"].get(key) != expected_identity.get(key):
                raise CompatibilityError(
                    f"capture source candidate identity differs: {key}"
                )
    return payload
