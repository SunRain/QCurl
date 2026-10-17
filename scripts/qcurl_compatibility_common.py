"""Shared file, producer and ELF checks for compatibility evidence."""

from __future__ import annotations

import json
import os
import platform
import re
import subprocess
from pathlib import Path
from typing import Any

if __package__:
    from .release_identity import file_sha256
else:
    from release_identity import file_sha256

LIBRARIES = {
    "Core": ("core", "libQCurl"),
    "OtherExtras": ("other-extras", "libQCurlOtherExtras"),
}
REFERENCE_SCHEMA = "qcurl-compatibility-reference@v1"
CHECK_SCHEMA = "qcurl-compatibility-check@v1"


class CompatibilityError(RuntimeError):
    """Compatibility inputs or execution cannot establish the required evidence."""


def load_json(path: Path) -> dict[str, Any]:
    """Read an object-valued evidence document, failing on malformed inputs."""
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise CompatibilityError(f"cannot read JSON {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise CompatibilityError(f"JSON root must be an object: {path}")
    return value


def write_json(path: Path, value: Any) -> None:
    """Write readable UTF-8 evidence without a BOM."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def new_output(path: Path) -> Path:
    """Reserve a new evidence directory without replacing existing results."""
    if path.exists() or path.is_symlink():
        raise CompatibilityError(f"output directory already exists: {path}")
    path.mkdir(parents=True)
    return path.resolve()


def run_logged(
    command: list[str], log: Path, *, env: dict[str, str] | None = None
) -> subprocess.CompletedProcess[str]:
    """Run a command and preserve its exact command, output and exit status."""
    try:
        result = subprocess.run(
            command, capture_output=True, text=True, env=env, timeout=180
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text(f"command={json.dumps(command)}\n{exc}\n", encoding="utf-8")
        raise CompatibilityError(f"command unavailable: {command[0]}: {exc}") from exc
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_text(
        f"command={json.dumps(command)}\nreturncode={result.returncode}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}",
        encoding="utf-8",
    )
    if result.returncode:
        raise CompatibilityError(
            f"command failed ({result.returncode}): {command}; log={log}"
        )
    return result


def checked_output(command: list[str]) -> str:
    """Read a required inspection tool's output without hiding its failure."""
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise CompatibilityError(f"inspection failed: {command}: {exc}") from exc
    if result.returncode:
        raise CompatibilityError(
            f"inspection failed: {command}: {result.stderr or result.stdout}"
        )
    return result.stdout


def relative_file(root: Path, relative: str) -> Path:
    """Resolve an indexed input while rejecting escaping paths and symlinks."""
    path = Path(relative)
    if path.is_absolute() or not path.parts or ".." in path.parts:
        raise CompatibilityError(f"invalid reference input path: {relative}")
    candidate = root / path
    if not candidate.resolve().is_relative_to(root.resolve()):
        raise CompatibilityError(f"reference input escapes its directory: {relative}")
    return candidate


def file_entry(root: Path, path: Path) -> dict[str, Any]:
    """Describe an actual input using the existing release content digest."""
    relative = path.relative_to(root).as_posix()
    relative_file(root, relative)
    if path.is_symlink():
        if not path.is_file():
            raise CompatibilityError(f"dangling reference symlink: {path}")
        return {"path": relative, "type": "symlink", "target": os.readlink(path)}
    if not path.is_file():
        raise CompatibilityError(f"reference input is not a regular file: {path}")
    return {
        "path": relative,
        "type": "file",
        "sha256": file_sha256(path),
        "size": path.stat().st_size,
        "executable": bool(path.stat().st_mode & 0o111),
    }


def inventory(
    root: Path, directories: tuple[str, ...], files: tuple[str, ...] = ()
) -> list[dict[str, Any]]:
    """Index all retained compatibility inputs and raw evidence files."""
    paths = [root / name for name in files]
    for directory in directories:
        folder = root / directory
        if not folder.is_dir():
            raise CompatibilityError(f"required reference directory missing: {folder}")
        paths.extend(
            path for path in folder.rglob("*") if path.is_file() or path.is_symlink()
        )
    return [file_entry(root, path) for path in sorted(paths)]


def validate_file_entries(root: Path, entries: Any) -> None:
    """Recompute every indexed input's bytes, link target and executable status."""
    if not isinstance(entries, list) or not entries:
        raise CompatibilityError("reference has no input inventory")
    seen: set[str] = set()
    for entry in entries:
        if not isinstance(entry, dict) or not isinstance(entry.get("path"), str):
            raise CompatibilityError("invalid reference inventory entry")
        relative = entry["path"]
        if relative in seen:
            raise CompatibilityError(f"duplicate reference input: {relative}")
        seen.add(relative)
        path = relative_file(root, relative)
        try:
            actual = file_entry(root, path)
        except OSError as exc:
            raise CompatibilityError(
                f"reference input missing: {relative}: {exc}"
            ) from exc
        if actual != entry:
            raise CompatibilityError(f"reference input content changed: {relative}")


def read_profile(path: Path) -> dict[str, str]:
    """Read the generated bracket-quoted producer consumer profile as data."""
    if not path.is_file():
        raise CompatibilityError(f"producer consumer profile missing: {path}")
    pattern = r'set\((\w+) \[(=*)\[(.*?)\]\2\] CACHE STRING "Producer consumer profile" FORCE\)'
    entries = re.findall(pattern, path.read_text(encoding="utf-8"), re.DOTALL)
    result = {name: value for name, _, value in entries}
    required = (
        "CMAKE_CXX_COMPILER",
        "CMAKE_BUILD_TYPE",
        "CMAKE_CXX_STANDARD",
        "CMAKE_CXX_EXTENSIONS",
        "QCURL_PRODUCER_COMPILER_ID",
        "QCURL_PRODUCER_COMPILER_VERSION",
        "QCURL_PRODUCER_QT_VERSION",
        "QCURL_PRODUCER_CURL_VERSION",
        "QCURL_PRODUCER_GLIBCXX_USE_CXX11_ABI",
        "QCURL_PRODUCER_VERSION",
        "QCURL_PRODUCER_ABI_VERSION",
        "QCURL_PRODUCER_SHARED",
    )
    if any(not result.get(name) for name in required):
        raise CompatibilityError(f"producer profile lacks required metadata: {path}")
    return result


def header_version(stage: Path) -> str:
    """Read the installed product version instead of the current source version."""
    header = stage / "include/qcurl/QCurlConfig.h"
    if not header.is_file():
        raise CompatibilityError(f"installed generated header missing: {header}")
    match = re.search(
        r'^#define QCURL_VERSION_STRING "(\d+\.\d+\.\d+)"$',
        header.read_text(encoding="utf-8"),
        re.MULTILINE,
    )
    if not match:
        raise CompatibilityError(f"invalid installed product version: {header}")
    return match[1]


def version_parts(value: str) -> tuple[int, int, int]:
    """Require a numeric three-part product version."""
    if not re.fullmatch(r"\d+\.\d+\.\d+", value):
        raise CompatibilityError(f"invalid product version: {value}")
    return tuple(int(part) for part in value.split("."))  # type: ignore[return-value]


def installed_libraries(stage: Path, version: str) -> dict[str, Path]:
    """Require both shared libraries, their correct SONAME chains and TestSupport."""
    line = ".".join(version.split(".")[:2])
    result = {}
    for component, (_, stem) in LIBRARIES.items():
        path = stage / "lib" / f"{stem}.so.{version}"
        if not path.is_file() or not path.resolve().is_relative_to(stage.resolve()):
            raise CompatibilityError(f"installed runtime library missing: {path}")
        dynamic = checked_output(["readelf", "-d", str(path)])
        if f"Library soname: [{stem}.so.{line}]" not in dynamic:
            raise CompatibilityError(f"wrong SONAME for {component}: {path}")
        for name in (f"{stem}.so", f"{stem}.so.{line}"):
            link = stage / "lib" / name
            if not link.is_symlink() or link.resolve() != path.resolve():
                raise CompatibilityError(f"invalid installed SONAME chain: {link}")
        result[component] = path
    if not (stage / "lib/libQCurlTestSupport.a").is_file():
        raise CompatibilityError("installed TestSupport static archive missing")
    return result


def _require_supported_profile(profile: dict[str, str], flags: str) -> None:
    supported = (
        profile["QCURL_PRODUCER_COMPILER_ID"] == "GNU"
        and profile["QCURL_PRODUCER_GLIBCXX_USE_CXX11_ABI"] == "1"
        and profile["CMAKE_BUILD_TYPE"] == "Release"
        and profile["CMAKE_CXX_STANDARD"] == "17"
        and profile["CMAKE_CXX_EXTENSIONS"] == "OFF"
        and profile["QCURL_PRODUCER_SHARED"] in ("ON", "TRUE", "1")
        and "sanitize" not in flags
    )
    if not supported:
        raise CompatibilityError(
            "unsupported compatibility tuple: requires uninstrumented Linux x86_64 GCC/libstdc++ Release shared ABI=1"
        )


def _require_supported_elf(libraries: dict[str, Path]) -> None:
    if platform.system() != "Linux":
        raise CompatibilityError("compatibility requires a Linux ELF producer")
    for library in libraries.values():
        elf = checked_output(["readelf", "-h", str(library)])
        if not re.search(
            r"^\s*Machine:\s*Advanced Micro Devices X86-64$", elf, re.MULTILINE
        ) or not re.search(r"^\s*Class:\s*ELF64$", elf, re.MULTILINE):
            raise CompatibilityError(
                f"runtime library has a different architecture tuple: {library}"
            )


def tuple_for_stage(stage: Path, profile: dict[str, str]) -> dict[str, Any]:
    """Recompute the supported ABI tuple from a retained producer profile and ELF."""
    version = header_version(stage)
    if profile["QCURL_PRODUCER_VERSION"] != version:
        raise CompatibilityError("producer profile and installed version differ")
    libraries = installed_libraries(stage, version)
    flags = (
        profile.get("CMAKE_CXX_FLAGS", "")
        + " "
        + profile.get("CMAKE_CXX_FLAGS_RELEASE", "")
    )
    _require_supported_profile(profile, flags)
    _require_supported_elf(libraries)
    config = (stage / "include/qcurl/QCurlConfig.h").read_text(encoding="utf-8")
    features = {
        name: bool(re.search(rf"^#\s*define {name}(?:\s|$)", config, re.MULTILINE))
        for name in ("QCURL_WEBSOCKET_SUPPORT", "QCURL_HTTP2_SUPPORT")
    }
    value = {
        "system": "Linux",
        "machine": "Advanced Micro Devices X86-64",
        "elfClass": "ELF64",
        "compilerId": profile["QCURL_PRODUCER_COMPILER_ID"],
        "compilerVersion": profile["QCURL_PRODUCER_COMPILER_VERSION"],
        "standardLibrary": "libstdc++",
        "cxxStandard": int(profile["CMAKE_CXX_STANDARD"]),
        "cxxExtensions": profile["CMAKE_CXX_EXTENSIONS"],
        "glibcxxCxx11Abi": "1",
        "buildType": "Release",
        "cxxFlags": flags.strip(),
        "linkerFlags": profile.get("CMAKE_EXE_LINKER_FLAGS", "")
        + " "
        + profile.get("CMAKE_EXE_LINKER_FLAGS_RELEASE", ""),
        "qtVersion": profile["QCURL_PRODUCER_QT_VERSION"],
        "curlVersion": profile["QCURL_PRODUCER_CURL_VERSION"],
        "websocket": profile.get("QCURL_PRODUCER_WEBSOCKET_SUPPORT", ""),
        "features": features,
    }
    return value


def producer_tuple(build: Path, stage: Path) -> tuple[dict[str, Any], dict[str, str]]:
    """Inspect a supported producer and bind its profile to the actual installed ELF."""
    profile = read_profile(build / "generated/consumer-profile.cmake")
    report = load_json(build / "evidence/package/shared-install-consumer.json")
    recorded = report.get("profile", {})
    if (
        report.get("stagePrefix") != str(stage.resolve())
        or recorded.get("path")
        != str((build / "generated/consumer-profile.cmake").resolve())
        or recorded.get("sha256")
        != file_sha256(build / "generated/consumer-profile.cmake")
    ):
        raise CompatibilityError(
            "package report does not bind the candidate stage to its producer profile"
        )
    value = tuple_for_stage(stage, profile)
    validate_install_inventory(stage, report)
    return value, profile


def validate_install_inventory(stage: Path, report: dict[str, Any]) -> None:
    """Check the existing package report against every installed file's actual bytes."""
    recorded = report.get("installInventory", {}).get("files")
    if not isinstance(recorded, list) or not recorded:
        raise CompatibilityError("package report lacks installed input content digests")
    actual = []
    for path in sorted(stage.rglob("*")):
        if path.is_file() or path.is_symlink():
            entry = file_entry(stage, path)
            entry.pop("executable", None)
            actual.append(entry)
    expected = [
        {key: value for key, value in entry.items() if key != "owners"}
        for entry in recorded
    ]
    if sorted(expected, key=lambda entry: entry["path"]) != actual:
        raise CompatibilityError(
            "installed input contents differ from the producer package report"
        )


def candidate_file_entries(build: Path, stage: Path) -> list[dict[str, Any]]:
    """Expose actual candidate inputs for independent release artifact verification."""
    inputs = inventory(stage, ("include", "lib"))
    entries = [{**entry, "path": str(stage / entry["path"])} for entry in inputs]
    for relative in (
        "CMakeCache.txt",
        "generated/consumer-profile.cmake",
        "evidence/package/shared-install-consumer.json",
    ):
        entry = file_entry(build, build / relative)
        entries.append({**entry, "path": str(build / relative)})
    return entries


def library_environment(stage: Path, profile: dict[str, str]) -> dict[str, str]:
    """Select the complete candidate runtime without ambient loader injection."""
    if any(os.environ.get(name) for name in ("LD_PRELOAD", "LD_AUDIT")):
        raise CompatibilityError("ambient LD_PRELOAD/LD_AUDIT is forbidden")
    directories = [stage / "lib"]
    for prefix in profile.get("CMAKE_PREFIX_PATH", "").split(";"):
        if prefix:
            directories.extend((Path(prefix) / "lib", Path(prefix) / "lib64"))
    for name in ("Qt6Core_DIR", "Qt6Network_DIR", "CURL_DIR"):
        if profile.get(name):
            directories.append(Path(profile[name]).parent.parent)
    for name in ("CURL_LIBRARY_RELEASE", "CURL_LIBRARY_DEBUG"):
        if profile.get(name) and not profile[name].endswith("-NOTFOUND"):
            directories.append(Path(profile[name]).parent)
    environment = dict(os.environ)
    environment.pop("LD_DEBUG_OUTPUT", None)
    environment["LD_LIBRARY_PATH"] = ":".join(
        dict.fromkeys(str(p.resolve()) for p in directories if p.is_dir())
    )
    environment["LD_DEBUG"] = "libs"
    return environment
