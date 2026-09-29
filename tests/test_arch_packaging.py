"""Exercise the local Arch entry point and its package-content failure paths."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]


def run(*args: str, cwd: Path, env: dict | None = None) -> subprocess.CompletedProcess:
    """Run a packaging command without hiding its exit code or diagnostics."""
    return subprocess.run(args, cwd=cwd, env=env, text=True, capture_output=True)


@pytest.fixture
def source(tmp_path: Path) -> Path:
    repo = tmp_path / "workspace with spaces"
    for name in (
        "packaging/arch/PKGBUILD",
        "packaging/arch/verify_package.cmake",
        "CMakeLists.txt",
        "src/CMakeLists.txt",
        "tests/public_api/surface_manifest.json",
        "LICENSE",
        "THIRD_PARTY_NOTICES.md",
    ):
        target = repo / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / name, target)
    return repo


def entry(
    source: Path, function: str, env: dict | None = None
) -> subprocess.CompletedProcess:
    """Invoke a real PKGBUILD function with makepkg's errexit semantics."""
    return run(
        "bash",
        "-ec",
        'source "$1"; "$2"',
        "arch-test",
        str(source / "packaging/arch/PKGBUILD"),
        function,
        cwd=source.parent,
        env=env,
    )


def test_prepare_accepts_current_workspace_from_another_directory(source: Path) -> None:
    result = entry(source, "prepare")
    assert result.returncode == 0, result.stderr


@pytest.mark.parametrize(
    "name", ["CMakeLists.txt", "LICENSE", "THIRD_PARTY_NOTICES.md"]
)
def test_prepare_rejects_missing_inputs(source: Path, name: str) -> None:
    (source / name).rename(source / f"{name}.saved")
    result = entry(source, "prepare")
    assert result.returncode != 0
    assert f"Required source file is missing: {source / name}" in result.stderr


@pytest.mark.parametrize("version", ["", "9.0.0"])
def test_prepare_rejects_missing_or_mismatched_version(
    source: Path, version: str
) -> None:
    (source / "CMakeLists.txt").write_text(
        (
            f"project(QCurl VERSION {version} LANGUAGES CXX)\n"
            if version
            else "# no project\n"
        ),
        encoding="utf-8",
    )
    result = entry(source, "prepare")
    assert result.returncode != 0
    assert "version" in result.stderr


@pytest.mark.parametrize("function", ["build", "package"])
def test_subcommand_failure_stops_entry(source: Path, function: str) -> None:
    commands = source.parent / "commands"
    commands.mkdir()
    cmake = commands / "cmake"
    cmake.write_text(
        '#!/bin/sh\nprintf "cmake failed\\n" >&2\nexit 73\n', encoding="utf-8"
    )
    cmake.chmod(0o755)
    env = dict(
        os.environ,
        PATH=f"{commands}:{os.environ['PATH']}",
        srcdir=str(source.parent / "build"),
        pkgdir=str(source.parent / "package"),
    )
    result = entry(source, function, env)
    assert result.returncode == 73, result.stderr
    assert result.stderr.count("cmake failed") == 1
    assert not (source.parent / "package").exists()


@pytest.fixture
def package(source: Path) -> Path:
    root = source.parent / "package"
    headers = json.loads(
        (source / "tests/public_api/surface_manifest.json").read_text()
    )["headers"]
    paths = [f"usr/include/qcurl/{entry['path']}" for entry in headers]
    paths += [
        f"usr/lib/cmake/QCurl/{name}.cmake"
        for name in (
            "QCurlConfig",
            "QCurlConfigVersion",
            "QCurlTargets",
            "QCurlTargets-release",
            "QCurlBlockingExtrasTargets",
            "QCurlOtherExtrasTargets",
            "QCurlOtherExtrasTargets-release",
            "QCurlTestSupportTargets",
            "QCurlTestSupportTargets-release",
        )
    ]
    paths += ["usr/lib/pkgconfig/qcurl.pc", "usr/lib/pkgconfig/qcurl-other-extras.pc"]
    for name in paths:
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("# fixture\n", encoding="utf-8")
    (root / "usr/include/qcurl/QCurlConfig.h").write_text(
        "// fixture\n#define QCURL_WEBSOCKET_SUPPORT\n", encoding="utf-8"
    )
    for name in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
        target = root / "usr/share/licenses/qcurl" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / name, target)
    for name in ("QCurl", "QCurlOtherExtras"):
        library = root / f"usr/lib/lib{name}.so.2.0.0"
        result = subprocess.run(
            [
                "cc",
                "-shared",
                "-fPIC",
                "-x",
                "c",
                "-",
                f"-Wl,-soname,lib{name}.so.2",
                "-o",
                str(library),
            ],
            input="int fixture(void) { return 0; }\n",
            text=True,
            capture_output=True,
        )
        assert result.returncode == 0, result.stderr
        (root / f"usr/lib/lib{name}.so.2").symlink_to(library.name)
        (root / f"usr/lib/lib{name}.so").symlink_to(f"lib{name}.so.2")
    native_object = source.parent / "test-support.o"
    subprocess.run(
        ["cc", "-c", "-x", "c", "-", "-o", str(native_object)],
        input="void test_support_fixture(void) {}\n",
        text=True,
        check=True,
    )
    subprocess.run(
        ["ar", "rcs", str(root / "usr/lib/libQCurlTestSupport.a"), str(native_object)],
        check=True,
    )
    return root


def verify(source: Path, package: Path) -> subprocess.CompletedProcess:
    """Check an install fixture with the same validator used by package()."""
    return run(
        "cmake",
        f"-DPACKAGE_ROOT={package}",
        "-DPACKAGE_VERSION=2.0.0",
        f"-DSOURCE_ROOT={source}",
        "-P",
        str(source / "packaging/arch/verify_package.cmake"),
        cwd=source.parent,
    )


def test_package_preserves_four_surfaces_and_static_test_support(
    source: Path, package: Path
) -> None:
    result = verify(source, package)
    assert result.returncode == 0, result.stderr
    assert "contents: PASS" in result.stdout


@pytest.mark.parametrize(
    "name",
    [
        "usr/lib/libQCurlTestSupport.a",
        "usr/include/qcurl/QCBlockingCookieStore.h",
        "usr/lib/cmake/QCurl/QCurlOtherExtrasTargets-release.cmake",
    ],
)
def test_package_rejects_missing_component_files(
    source: Path, package: Path, name: str
) -> None:
    (package / name).rename(source.parent / "missing-file.saved")
    result = verify(source, package)
    assert result.returncode != 0
    assert "Required package file is missing" in result.stderr


@pytest.mark.parametrize(
    "name",
    [
        "usr/lib/libQCurlBlockingExtras.so",
        "usr/lib/libQCurl.a",
        "usr/lib/libcurl.so",
        "usr/include/qcurl/QCNetworkReply_p.h",
        "usr/bin/test-program",
    ],
)
def test_package_rejects_forbidden_files(
    source: Path, package: Path, name: str
) -> None:
    path = package / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("unwanted", encoding="utf-8")
    result = verify(source, package)
    assert result.returncode != 0
    assert "Forbidden package file" in result.stderr


def test_package_rejects_broken_symlink(source: Path, package: Path) -> None:
    link = package / "usr/lib/libQCurl.so"
    link.rename(source.parent / "link.saved")
    link.symlink_to("not-in-package.so")
    result = verify(source, package)
    assert result.returncode != 0
    assert "Unexpected library symlink" in result.stderr


def test_package_rejects_producer_path_leak(source: Path, package: Path) -> None:
    (package / "usr/lib/pkgconfig/qcurl.pc").write_text(
        f"prefix={source}\n", encoding="utf-8"
    )
    result = verify(source, package)
    assert result.returncode != 0
    assert "Producer path leaked" in result.stderr


def test_package_rejects_test_support_without_native_code(
    source: Path, package: Path
) -> None:
    library = package / "usr/lib/libQCurlTestSupport.a"
    library.rename(source.parent / "test-support.saved.a")
    subprocess.run(["ar", "rcs", str(library)], check=True)
    result = verify(source, package)
    assert result.returncode != 0
    assert "TestSupport archive has no usable native code" in result.stderr


def test_package_without_websocket_keeps_other_extras(
    source: Path, package: Path
) -> None:
    (package / "usr/include/qcurl/QCurlConfig.h").write_text(
        "// WebSocket disabled\n", encoding="utf-8"
    )
    saved = source.parent / "websocket-headers"
    saved.mkdir()
    for path in (package / "usr/include/qcurl").glob("QCWebSocket*.h"):
        path.rename(saved / path.name)
    result = verify(source, package)
    assert result.returncode == 0, result.stderr
