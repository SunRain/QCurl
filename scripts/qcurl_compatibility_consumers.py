"""Build retained consumer sources once and inspect each real loader execution."""

from __future__ import annotations

import re
import shutil
from pathlib import Path
from typing import Any

if __package__:
    from .qcurl_compatibility_common import CompatibilityError, LIBRARIES
    from .qcurl_compatibility_common import (
        checked_output,
        installed_libraries,
        library_environment,
    )
    from .qcurl_compatibility_common import run_logged
    from .release_identity import file_sha256
    from .release_package_evidence import CONSUMERS
else:
    from qcurl_compatibility_common import CompatibilityError, LIBRARIES
    from qcurl_compatibility_common import (
        checked_output,
        installed_libraries,
        library_environment,
    )
    from qcurl_compatibility_common import run_logged
    from release_identity import file_sha256
    from release_package_evidence import CONSUMERS


def copy_consumer_sources(repo: Path, output: Path) -> dict[str, dict[str, str]]:
    """Retain all four installed-consumer source trees and their executable names."""
    consumers = {}
    for spec in CONSUMERS:
        source = repo / "tests/public_api" / spec.source_dir
        destination = output / "sources" / spec.target
        shutil.copytree(source, destination)
        consumers[spec.target] = {
            "source": f"sources/{spec.target}",
            "binary": f"bin/{spec.target}",
            "executable": spec.executable,
        }
    return consumers


def inspect_binary(binary: Path, *, component: str) -> None:
    """Reject old-library RPATHs and require the TestSupport-to-Core static bridge."""
    dynamic = checked_output(["readelf", "-d", str(binary)])
    if "(RPATH)" in dynamic or "(RUNPATH)" in dynamic:
        raise CompatibilityError(
            f"retained consumer must not contain RPATH/RUNPATH: {binary}"
        )
    if component == "TestSupport":
        symbols = checked_output(
            ["nm", "-D", "--undefined-only", "--demangle", str(binary)]
        )
        if "QCurl::installNetworkMockProvider(" not in symbols:
            raise CompatibilityError(
                "TestSupport consumer lacks the retained static-to-Core bridge"
            )


def build_consumers(
    source_root: Path,
    consumers: dict[str, Any],
    profile: Path,
    stage: Path,
    output: Path,
) -> dict[str, dict[str, str]]:
    """Rebuild retained sources in an explicit operation separate from old-binary checks."""
    result = {}
    for component, consumer in consumers.items():
        source = source_root / consumer["source"]
        build = output / "build" / component
        configure = [
            "cmake",
            "-C",
            str(profile),
            "-S",
            str(source),
            "-B",
            str(build),
            f"-DQCURL_STAGE_PREFIX={stage}",
            "-DCMAKE_SKIP_RPATH=ON",
            "-DCMAKE_BUILD_RPATH=",
            "-DCMAKE_INSTALL_RPATH=",
        ]
        run_logged(configure, output / "logs/build" / f"{component}.configure.log")
        run_logged(
            ["cmake", "--build", str(build), "--parallel", "2"],
            output / "logs/build" / f"{component}.build.log",
        )
        binary = output / "bin" / component
        binary.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(build / consumer["executable"], binary)
        inspect_binary(binary, component=component)
        result[component] = {
            "binary": f"bin/{component}",
            "source": consumer["source"],
            "executable": consumer["executable"],
        }
    return result


def _loaded_paths(stderr: str) -> dict[str, Path]:
    paths = {}
    for value in re.findall(r"calling init:\s+([^\s]+)", stderr):
        path = Path(value)
        if not path.is_absolute() or not path.is_file():
            raise CompatibilityError(
                f"loader reported an unusable library path: {value}"
            )
        paths[path.name] = path.resolve()
    if not paths:
        raise CompatibilityError(
            "loader trace contains no actual library initialization"
        )
    return paths


def _runtime_inputs(paths: dict[str, Path]) -> dict[str, dict[str, Any]]:
    return {
        name: {
            "path": str(path),
            "sha256": file_sha256(path),
            "size": path.stat().st_size,
        }
        for name, path in sorted(paths.items())
        if not name.startswith("libQCurl")
    }


def _validate_loader(
    paths: dict[str, Path], stage: Path, version: str, component: str
) -> None:
    libraries = installed_libraries(stage, version)
    allowed = {path.resolve() for path in libraries.values()}
    loaded = {path for name, path in paths.items() if name.startswith("libQCurl")}
    required = {libraries["Core"].resolve()}
    if component == "OtherExtras":
        required.add(libraries["OtherExtras"].resolve())
    if not required.issubset(loaded) or not loaded.issubset(allowed):
        raise CompatibilityError(
            f"consumer {component} loaded wrong QCurl libraries: {sorted(map(str, loaded))}; expected={sorted(map(str, required))}"
        )


def run_consumers(
    binary_root: Path,
    consumers: dict[str, Any],
    stage: Path,
    version: str,
    profile: dict[str, str],
    output: Path,
    *,
    expected: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Run the unchanged ELF files and verify actual QCurl and dependency paths."""
    results = {}
    environment = library_environment(stage, profile)
    for component, consumer in consumers.items():
        binary = binary_root / consumer["binary"]
        inspect_binary(binary, component=component)
        before = file_sha256(binary)
        result = run_logged(
            [str(binary)], output / f"{component}.run.log", env=environment
        )
        loader = output / f"{component}.loader.log"
        loader.write_text(result.stderr, encoding="utf-8")
        paths = _loaded_paths(result.stderr)
        _validate_loader(paths, stage, version, component)
        dependencies = _runtime_inputs(paths)
        if expected is not None:
            wanted = expected[component]["dependencyInputs"]
            actual = {key: entry["sha256"] for key, entry in dependencies.items()}
            wanted_digests = {key: entry["sha256"] for key, entry in wanted.items()}
            if actual != wanted_digests:
                raise CompatibilityError(
                    f"runtime dependency tuple changed for {component}"
                )
        after = file_sha256(binary)
        if after != before:
            raise CompatibilityError(
                f"retained consumer was replaced during execution: {component}"
            )
        results[component] = {
            "result": "pass",
            "returncode": result.returncode,
            "binary": str(binary),
            "binarySha256": before,
            "loaderLog": str(loader),
            "loaderSha256": file_sha256(loader),
            "loadedLibraries": {key: str(path) for key, path in sorted(paths.items())},
            "dependencyInputs": dependencies,
            "testSupportBridge": component == "TestSupport",
        }
    return results
