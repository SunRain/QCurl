"""按 producer profile 核对安装消费者的真实运行依赖。"""

from __future__ import annotations

from pathlib import Path
import re
from typing import Callable

if __package__:
    from .qcurl_compatibility_common import CompatibilityError
else:
    from qcurl_compatibility_common import CompatibilityError


def _dependency_paths(profile: dict[str, str]) -> dict[str, Path]:
    if not profile.get("Qt6Core_DIR"):
        raise CompatibilityError("producer profile lacks the Qt runtime location")
    result = {
        "libQt6Core.so.6": Path(profile["Qt6Core_DIR"]).parent.parent
        / "libQt6Core.so.6"
    }
    if profile.get("Qt6Network_DIR"):
        result["libQt6Network.so.6"] = (
            Path(profile["Qt6Network_DIR"]).parent.parent / "libQt6Network.so.6"
        )
    curl_library = profile.get("CURL_LIBRARY_RELEASE")
    if not curl_library and profile.get("CURL_DIR"):
        curl_library = str(Path(profile["CURL_DIR"]).parent.parent / "libcurl.so")
    if not curl_library or curl_library.endswith("-NOTFOUND"):
        raise CompatibilityError("producer profile lacks the libcurl runtime location")
    result["libcurl.so.4"] = Path(curl_library)
    if any(not path.is_file() for path in result.values()):
        raise CompatibilityError("producer runtime dependency is missing")
    return result


def _check_dependencies(libraries: dict[str, str], profile: dict[str, str]) -> None:
    expected = _dependency_paths(profile)
    for name, path in expected.items():
        if name == "libcurl.so.4" and path.suffix == ".a":
            if name in libraries:
                raise CompatibilityError(
                    "static libcurl consumer loaded a shared libcurl"
                )
            continue
        if name == "libQt6Network.so.6" and name not in libraries:
            continue
        if libraries.get(name) != str(path.resolve()):
            raise CompatibilityError(
                f"consumer loaded a different producer dependency: {name}"
            )
    unexpected = set(libraries) - set(expected)
    if unexpected:
        raise CompatibilityError(
            f"consumer loaded unrecorded dependencies: {unexpected}"
        )


def consumer_runtime_libraries(
    executable: Path,
    stage_dir: Path,
    *,
    linkage: str,
    component: str,
    profile: dict[str, str],
    run_command: Callable,
) -> dict[str, str]:
    """拒绝宿主或旧包偷渡，并返回实际 QCurl、Qt 和 libcurl 路径。"""
    completed = run_command(["ldd", str(executable)], capture_output=True)
    if completed.returncode:
        raise CompatibilityError(f"cannot inspect consumer loader: {completed.stderr}")
    libraries: dict[str, str] = {}
    for line in completed.stdout.splitlines():
        if not any(
            name in line
            for name in ("libQCurl", "libQt6Core", "libQt6Network", "libcurl.so")
        ):
            continue
        match = re.match(r"\s*(\S+)\s+=>\s+(\S+)\s+\(0x", line)
        if match is None:
            raise CompatibilityError(f"unresolved consumer runtime library: {line}")
        name, loaded = match.groups()
        path = Path(loaded).resolve()
        if name.startswith("libQCurl") and not path.is_relative_to(stage_dir.resolve()):
            raise CompatibilityError(
                f"consumer loaded QCurl outside install stage: {path}"
            )
        libraries[name] = str(path)
    expected = {"libQCurl"}
    if component == "OtherExtras":
        expected.add("libQCurlOtherExtras")
    actual = {
        name.split(".so", 1)[0] for name in libraries if name.startswith("libQCurl")
    }
    if actual != (expected if linkage == "shared" else set()):
        raise CompatibilityError(
            f"unexpected {component} consumer runtime libraries: {actual}"
        )
    _check_dependencies(
        {
            name: path
            for name, path in libraries.items()
            if not name.startswith("libQCurl")
        },
        profile,
    )
    return libraries
