"""Validate the real installed package's version matching and Linux runtime identities."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess


def _checked(command: list[str], **kwargs) -> str:
    result = subprocess.run(command, text=True, capture_output=True, **kwargs)
    if result.returncode:
        raise RuntimeError(f"command failed: {command}\n{result.stdout}{result.stderr}")
    return result.stdout


def _check_sonames(stage: Path, version: str) -> None:
    abi = ".".join(version.split(".")[:2])
    for name in ("QCurl", "QCurlOtherExtras"):
        real_libraries = list(stage.rglob(f"lib{name}.so.{version}"))
        if len(real_libraries) != 1:
            raise RuntimeError(f"expected one installed {name} shared library")
        library = real_libraries[0]
        dynamic = _checked(["readelf", "-d", str(library)])
        if not re.search(rf"\(SONAME\).*\[lib{name}\.so\.{re.escape(abi)}\]", dynamic):
            raise RuntimeError(f"wrong SONAME for {library}")
        expected = {f"lib{name}.so", f"lib{name}.so.{abi}", f"lib{name}.so.{version}"}
        actual = {path.name for path in library.parent.glob(f"lib{name}.so*")}
        if actual != expected:
            raise RuntimeError(f"wrong {name} library/link set: {actual}")
        for link_name, target in (
            (f"lib{name}.so", f"lib{name}.so.{abi}"),
            (f"lib{name}.so.{abi}", library.name),
        ):
            link = library.parent / link_name
            if not link.is_symlink() or link.readlink().as_posix() != target:
                raise RuntimeError(f"wrong {name} library symlink: {link}")


def _check_cmake_requests(
    stage: Path, producer: Path, build: Path, version: str
) -> None:
    major, minor, patch = (int(value) for value in version.split("."))
    requests = [
        (version, True),
        (f"{major}.{minor}.0", True),
        (f"{major}.{minor}.{patch + 1}", False),
        (f"{major}.{minor + 1}.0", False),
        (f"{major}.{minor - 1}.0", False),
        (f"{major + 1}.0.0", False),
    ]
    for index, (minimum, accepted) in enumerate(requests):
        result = subprocess.run(
            [
                "cmake",
                "-C",
                str(producer / "generated/consumer-profile.cmake"),
                "-S",
                str(Path(__file__).parent / "consumer_version"),
                "-B",
                str(build / str(index)),
                f"-DQCURL_STAGE_PREFIX={stage}",
                f"-DQCURL_REQUEST_VERSION={minimum}",
                f"-DQCURL_EXPECTED_VERSION={version}",
            ],
            text=True,
            capture_output=True,
        )
        if (result.returncode == 0) != accepted:
            raise RuntimeError(
                f"incorrect installed CMake request {minimum}:\n{result.stdout}{result.stderr}"
            )
        if not accepted and "not compatible" not in result.stderr:
            raise RuntimeError(
                f"request {minimum} failed for another reason:\n{result.stderr}"
            )


def _check_pkg_config_requests(stage: Path, version: str) -> None:
    major, minor, patch = (int(value) for value in version.split("."))
    pc = list(stage.rglob("qcurl.pc"))
    if len(pc) != 1:
        raise RuntimeError("expected one installed qcurl.pc")
    env = dict(os.environ, PKG_CONFIG_PATH=str(pc[0].parent))
    for package in ("qcurl", "qcurl-other-extras"):
        if (
            _checked(["pkg-config", "--modversion", package], env=env).strip()
            != version
        ):
            raise RuntimeError(f"wrong installed {package} version")
        _checked(
            [
                "pkg-config",
                "--exists",
                f"{package} >= {version}",
                f"{package} < {major}.{minor + 1}.0",
            ],
            env=env,
        )
        rejected = subprocess.run(
            ["pkg-config", "--exists", f"{package} >= {major}.{minor}.{patch + 1}"],
            env=env,
        )
        if rejected.returncode == 0:
            raise RuntimeError(f"{package} accepted a newer minimum patch")


def validate_installed_versions(stage: Path, producer: Path, build: Path) -> None:
    """Check actual candidate matching, distinct from synthetic future-patch fixtures."""

    config = (producer / "src/QCurlConfig.h").read_text(encoding="utf-8")
    match = re.search(r'#define QCURL_VERSION_STRING "(\d+)\.(\d+)\.(\d+)"', config)
    if match is None:
        raise RuntimeError("producer generated header has no release version")
    major, minor, patch = (int(value) for value in match.groups())
    version = f"{major}.{minor}.{patch}"
    _check_cmake_requests(stage, producer, build, version)
    _check_pkg_config_requests(stage, version)
    if list(stage.rglob("libQCurl.so*")):
        _check_sonames(stage, version)
    print(
        f"[public_api] installed version contract passed: {version}, SONAME {major}.{minor}"
    )


def main() -> None:
    """Validate an installed producer using independent CMake and pkg-config requests."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage-dir", type=Path, required=True)
    parser.add_argument("--producer-build-dir", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()
    validate_installed_versions(
        args.stage_dir.resolve(),
        args.producer_build_dir.resolve(),
        args.build_dir.resolve(),
    )


if __name__ == "__main__":
    main()
