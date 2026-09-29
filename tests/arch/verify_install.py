#!/usr/bin/env python3
"""Run isolated CMake/pkg-config consumers against an unpacked or installed Arch package."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from scripts.release_package_evidence import CONSUMERS  # noqa: E402


def checked(command: list[str], env: dict[str, str]) -> str:
    """Run a consumer command, including complete diagnostics when it fails."""
    result = subprocess.run(command, env=env, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(
            f"Command failed ({result.returncode}): {shlex.join(command)}\n"
            f"{result.stdout}{result.stderr}"
        )
    return result.stdout.strip()


def check_loaded_libraries(executable: Path, prefix: Path, env: dict[str, str]) -> None:
    """Reject consumers that load QCurl from another installation or build tree."""
    output = checked(["ldd", str(executable)], env)
    found = False
    for line in output.splitlines():
        if "libQCurl" not in line:
            continue
        found = True
        if " => " not in line or "not found" in line:
            raise RuntimeError(f"Unresolved QCurl library: {line}")
        path = Path(line.split(" => ", 1)[1].split(" (0x", 1)[0].strip()).resolve()
        if path.parent != (prefix / "lib").resolve():
            raise RuntimeError(f"Consumer loaded a different QCurl: {path}")
    if not found:
        raise RuntimeError(f"Consumer did not link QCurl: {executable}")


def cmake_command(source: Path, build: Path, prefix: Path) -> list[str]:
    """Pin QCurl discovery to the package under test, leaving system dependencies available."""
    return [
        "cmake",
        "-S",
        str(source),
        "-B",
        str(build),
        "-G",
        "Ninja",
        f"-DQCURL_STAGE_PREFIX={prefix}",
        f"-DQCurl_DIR={prefix / 'lib/cmake/QCurl'}",
        "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF",
        "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF",
    ]


def cmake_consumers(prefix: Path, work: Path, env: dict[str, str]) -> None:
    """Build and execute the existing four component consumers and discovery boundary checks."""
    for spec in CONSUMERS:
        build = work / spec.target
        checked(
            cmake_command(ROOT / "tests/public_api" / spec.source_dir, build, prefix),
            env,
        )
        checked(["cmake", "--build", str(build), "--parallel", "2"], env)
        executable = build / spec.executable
        check_loaded_libraries(executable, prefix, env)
        checked([str(executable)], env)
        print(f"CMake {spec.target}: PASS", flush=True)

    contract = ROOT / "tests/arch/consumer"
    checked(cmake_command(contract, work / "component-contract", prefix), env)
    negative = subprocess.run(
        cmake_command(contract, work / "unknown-component", prefix)
        + ["-DQCURL_REQUIRE_UNKNOWN=ON"],
        env=env,
        text=True,
        capture_output=True,
    )
    if negative.returncode == 0 or "QCurl_FOUND" not in negative.stderr:
        raise RuntimeError(
            f"Unknown REQUIRED component did not fail as expected:\n{negative.stdout}{negative.stderr}"
        )
    print(
        "Default Core / combined components / private header / unknown component: PASS",
        flush=True,
    )


def pkgconfig_consumers(prefix: Path, work: Path, env: dict[str, str]) -> None:
    """Compile and run both existing pkg-config surfaces without CMake supplying link flags."""
    programs = {
        "qcurl": """#include <QCNetworkRequest.h>
#include <QUrl>
int main() {
    QCurl::QCNetworkRequest request(QUrl{QStringLiteral("https://example.invalid")});
    return request.url().scheme() == QStringLiteral("https") ? 0 : 1;
}
""",
        "qcurl-other-extras": """#include <QCNetworkMiddlewareExtras.h>
#include <QString>
int main() {
    QCurl::QCObservabilityMiddleware middleware;
    return middleware.name() == QStringLiteral("QCObservabilityMiddleware") ? 0 : 1;
}
""",
    }
    core_version = checked(["pkg-config", "--modversion", "qcurl"], env)
    for name, program in programs.items():
        pcdir = Path(
            checked(["pkg-config", "--variable=pcfiledir", name], env)
        ).resolve()
        if pcdir != (prefix / "lib/pkgconfig").resolve():
            raise RuntimeError(f"Wrong pkg-config input: {pcdir}")
        if checked(["pkg-config", "--modversion", name], env) != core_version:
            raise RuntimeError("pkg-config component versions differ")
        source = work / f"{name}.cpp"
        source.write_text(program, encoding="utf-8")
        executable = work / name
        flags = shlex.split(checked(["pkg-config", "--cflags", "--libs", name], env))
        checked(
            [
                "c++",
                "-std=c++17",
                "-fPIC",
                "-DQT_NO_KEYWORDS",
                str(source),
                "-o",
                str(executable),
                *flags,
            ],
            env,
        )
        check_loaded_libraries(executable, prefix, env)
        checked([str(executable)], env)
        print(f"pkg-config {name} {core_version}: PASS", flush=True)


def main() -> int:
    """Verify a prefix in a new work directory, retaining all outputs for diagnosis."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--prefix",
        type=Path,
        required=True,
        help="Package's usr directory, or /usr after installation",
    )
    parser.add_argument(
        "--work-dir",
        type=Path,
        required=True,
        help="New directory; never deletes previous results",
    )
    args = parser.parse_args()
    prefix = args.prefix.resolve(strict=True)
    work = args.work_dir.resolve()
    if not (prefix / "lib/cmake/QCurl/QCurlConfig.cmake").is_file():
        parser.error("prefix has no QCurl CMake package")
    work.mkdir(parents=True, exist_ok=False)
    env = dict(os.environ)
    for key in (
        "LD_PRELOAD",
        "CMAKE_PREFIX_PATH",
        "QCurl_DIR",
        "PKG_CONFIG_SYSROOT_DIR",
    ):
        env.pop(key, None)
    env.update(
        LD_LIBRARY_PATH=str(prefix / "lib"),
        PKG_CONFIG_PATH=str(prefix / "lib/pkgconfig"),
        PKG_CONFIG_LIBDIR="/usr/lib/pkgconfig:/usr/share/pkgconfig",
    )
    cmake_consumers(prefix, work, env)
    pkgconfig_consumers(prefix, work, env)
    print(
        "Installed QCurl consumers: PASS (4 CMake, component boundaries, 2 pkg-config)"
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
