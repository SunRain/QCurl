"""Build small, real ELF inputs once for release-schema and failure tests."""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path
from typing import Any

from scripts.qcurl_compatibility_common import LIBRARIES, REFERENCE_SCHEMA
from scripts.qcurl_compatibility_common import (
    file_entry,
    inventory,
    producer_tuple,
    write_json,
)
from scripts.qcurl_compatibility_consumers import run_consumers
from scripts.qcurl_compatibility_reference import INPUT_DIRECTORIES, validate_reference
from scripts.release_identity import file_sha256
from scripts.release_package_evidence import CONSUMERS


def _run(command: list[str]) -> None:
    subprocess.run(command, check=True, capture_output=True, text=True)


def _profile(build: Path, version: str) -> Path:
    compiler = shutil.which("g++")
    if compiler is None:
        raise RuntimeError("real GCC is required by compatibility fixtures")
    compiler_version = subprocess.run(
        [compiler, "-dumpfullversion"], check=True, capture_output=True, text=True
    ).stdout.strip()
    values = {
        "CMAKE_CXX_COMPILER": compiler,
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_CXX_FLAGS": "",
        "CMAKE_CXX_STANDARD": "17",
        "CMAKE_CXX_STANDARD_REQUIRED": "ON",
        "CMAKE_CXX_EXTENSIONS": "OFF",
        "CMAKE_CXX_FLAGS_RELEASE": "-O3 -DNDEBUG -g",
        "CMAKE_PREFIX_PATH": "",
        "QCURL_PRODUCER_COMPILER_ID": "GNU",
        "QCURL_PRODUCER_COMPILER_VERSION": compiler_version,
        "QCURL_PRODUCER_QT_VERSION": "6.12.0",
        "QCURL_PRODUCER_CURL_VERSION": "8.23.0",
        "QCURL_PRODUCER_GLIBCXX_USE_CXX11_ABI": "1",
        "QCURL_PRODUCER_SHARED": "ON",
        "QCURL_PRODUCER_VERSION": version,
        "QCURL_PRODUCER_ABI_VERSION": ".".join(version.split(".")[:2]),
        "QCURL_PRODUCER_WEBSOCKET_SUPPORT": "ON",
    }
    profile = build / "generated/consumer-profile.cmake"
    profile.parent.mkdir(parents=True)
    profile.write_text(
        "".join(
            f'set({name} [=[{value}]=] CACHE STRING "Producer consumer profile" FORCE)\n'
            for name, value in values.items()
        ),
        encoding="utf-8",
    )
    (build / "CMakeCache.txt").write_text(
        f"CMAKE_CXX_COMPILER:FILEPATH={compiler}\nCMAKE_BUILD_TYPE:STRING=Release\n",
        encoding="utf-8",
    )
    return profile


def _libraries(directory: Path, build: Path, stage: Path, version: str) -> None:
    (build / "src").mkdir(parents=True)
    (stage / "lib").mkdir(parents=True)
    (stage / "include/qcurl").mkdir(parents=True)
    (stage / "include/qcurl/QCurlConfig.h").write_text(
        f'#define QCURL_VERSION_STRING "{version}"\n', encoding="utf-8"
    )
    core = directory / "core.cpp"
    core.write_text(
        "namespace QCurl { bool installNetworkMockProvider(const void *) noexcept { return true; } }\n"
        'extern "C" int core_contract() { return 21; }\n',
        encoding="utf-8",
    )
    other = directory / "other.cpp"
    other.write_text(
        'extern "C" int other_contract() { return 42; }\n', encoding="utf-8"
    )
    line = ".".join(version.split(".")[:2])
    for component, (_, stem) in LIBRARIES.items():
        output = build / "src" / f"{stem}.so.{version}"
        source = core if component == "Core" else other
        _run(
            [
                "g++",
                "-std=c++17",
                "-O3",
                "-g",
                "-fPIC",
                "-shared",
                str(source),
                f"-Wl,-soname,{stem}.so.{line}",
                "-o",
                str(output),
            ]
        )
        shutil.copy2(output, stage / "lib" / output.name)
        for suffix in ("", f".{line}"):
            (stage / "lib" / f"{stem}.so{suffix}").symlink_to(output.name)
    support = directory / "support.cpp"
    support.write_text(
        "namespace QCurl { bool installNetworkMockProvider(const void *) noexcept; }\n"
        "int support_contract() { return QCurl::installNetworkMockProvider(nullptr) ? 21 : 0; }\n",
        encoding="utf-8",
    )
    _run(
        [
            "g++",
            "-std=c++17",
            "-g",
            "-c",
            str(support),
            "-o",
            str(directory / "support.o"),
        ]
    )
    _run(
        [
            "ar",
            "rcs",
            str(stage / "lib/libQCurlTestSupport.a"),
            str(directory / "support.o"),
        ]
    )


def _consumers(root: Path, stage: Path) -> dict[str, Any]:
    consumers = {}
    for spec in CONSUMERS:
        source = root / "sources" / spec.target
        source.mkdir(parents=True)
        declarations = 'extern "C" int core_contract();\n'
        expression = "core_contract() == 21"
        flags = []
        if spec.target == "OtherExtras":
            declarations += 'extern "C" int other_contract();\n'
            expression += " && other_contract() == 42"
            flags += ["-lQCurlOtherExtras"]
        elif spec.target == "TestSupport":
            declarations += "int support_contract();\n"
            expression += " && support_contract() == 21"
            flags += [str(stage / "lib/libQCurlTestSupport.a")]
        (source / "main.cpp").write_text(
            declarations + f"int main() {{ return ({expression}) ? 0 : 19; }}\n",
            encoding="utf-8",
        )
        cmake = f"cmake_minimum_required(VERSION 3.21)\nproject(synthetic_{spec.target} LANGUAGES CXX)\nadd_executable({spec.executable} main.cpp)\n"
        libs = '"${QCURL_STAGE_PREFIX}/lib/libQCurl.so"'
        if spec.target == "OtherExtras":
            libs += ' "${QCURL_STAGE_PREFIX}/lib/libQCurlOtherExtras.so"'
        if spec.target == "TestSupport":
            libs = '"${QCURL_STAGE_PREFIX}/lib/libQCurlTestSupport.a" ' + libs
        (source / "CMakeLists.txt").write_text(
            cmake + f"target_link_libraries({spec.executable} PRIVATE {libs})\n",
            encoding="utf-8",
        )
        binary = root / "bin" / spec.target
        binary.parent.mkdir(exist_ok=True)
        _run(
            [
                "g++",
                "-std=c++17",
                "-O3",
                "-g",
                str(source / "main.cpp"),
                f"-L{stage / 'lib'}",
                *flags,
                "-lQCurl",
                "-o",
                str(binary),
            ]
        )
        consumers[spec.target] = {
            "source": f"sources/{spec.target}",
            "binary": f"bin/{spec.target}",
            "executable": spec.executable,
        }
    return consumers


def make_reference_fixture(
    directory: Path, *, version: str = "2.1.0", head: str = "a" * 40
) -> tuple[Path, Path, Path]:
    """Return (reference, producer, stage) with real ELFs and explicit synthetic provenance."""
    directory.mkdir(parents=True, exist_ok=True)
    root, build, stage = (
        directory / "reference",
        directory / "producer",
        directory / "stage",
    )
    root.mkdir()
    profile = _profile(build, version)
    _libraries(directory, build, stage, version)
    package = build / "evidence/package/shared-install-consumer.json"
    installed = []
    for path in sorted(stage.rglob("*")):
        if path.is_file() or path.is_symlink():
            entry = file_entry(stage, path)
            entry.pop("executable", None)
            installed.append(entry)
    write_json(
        package,
        {
            "stagePrefix": str(stage.resolve()),
            "profile": {"path": str(profile.resolve()), "sha256": file_sha256(profile)},
            "installInventory": {"files": installed},
        },
    )
    abi_tuple, parsed_profile = producer_tuple(build, stage)
    shutil.copytree(stage, root / "install", symlinks=True)
    (root / "profile").mkdir()
    for source, name in (
        (profile, "consumer-profile.cmake"),
        (build / "CMakeCache.txt", "CMakeCache.txt"),
        (package, "package-install-consumer.json"),
    ):
        shutil.copy2(source, root / "profile" / name)
    write_json(
        root / "profile/surface-manifest.json",
        {"syntheticToolFixture": True, "headers": []},
    )
    consumers = _consumers(root, stage)
    libraries = {}
    (root / "abi").mkdir()
    for component, (slug, stem) in LIBRARIES.items():
        library = root / "install/lib" / f"{stem}.so.{version}"
        snapshot = root / "abi" / f"{slug}.snapshot.xml"
        snapshot.write_text(
            subprocess.run(
                ["abidw", "--show-locs", str(library)],
                check=True,
                text=True,
                capture_output=True,
            ).stdout,
            encoding="utf-8",
        )
        write_json(
            root / "abi" / f"{slug}.dynamic-symbols.json",
            {"syntheticToolFixture": True, "component": slug},
        )
        libraries[component] = {
            "library": library.relative_to(root).as_posix(),
            "snapshot": f"abi/{slug}.snapshot.xml",
            "symbols": f"abi/{slug}.dynamic-symbols.json",
        }
    runtime = run_consumers(
        root,
        consumers,
        root / "install",
        version,
        parsed_profile,
        root / "logs/selftest",
    )
    write_json(
        root / "validation.json",
        {
            "schema": "qcurl-reference-validation@v1",
            "result": "REFERENCE_VALIDATED",
            "referenceSelfTest": runtime,
            "syntheticToolFixture": True,
        },
    )
    payload = {
        "schema": REFERENCE_SCHEMA,
        "version": version,
        "line": ".".join(version.split(".")[:2]),
        "identity": {
            "head": head,
            "submodules": {"entries": [], "digest": "synthetic-tool-fixture"},
            "tracked_patch": {
                "entries": [],
                "patch_digest": "synthetic-tool-fixture",
                "content_digest": "synthetic-tool-fixture",
            },
            "untracked": {"entries": [], "digest": "synthetic-tool-fixture"},
        },
        "origin": {
            "buildDir": str(build),
            "stagePrefix": str(stage),
            "packageReport": str(package),
            "packageReportSha256": file_sha256(package),
            "profile": str(profile),
            "profileSha256": file_sha256(profile),
        },
        "tuple": abi_tuple,
        "libraries": libraries,
        "consumers": consumers,
        "profile": "profile/consumer-profile.cmake",
        "validation": "validation.json",
        "syntheticToolFixture": True,
    }
    payload["files"] = inventory(root, INPUT_DIRECTORIES, ("validation.json",))
    write_json(root / "reference.json", payload)
    validate_reference(root)
    return root, build, stage
