from __future__ import annotations

import json
import subprocess
from pathlib import Path

import pytest

from scripts import release_package_evidence


def _contract() -> dict[str, object]:
    return {
        "deliveryTargets": {
            spec.target: {
                "consumerTests": {
                    "shared": spec.test_name,
                    "static": spec.test_name,
                }
            }
            for spec in release_package_evidence.CONSUMERS
        }
    }


def _surface_manifest() -> dict[str, object]:
    components = (
        "Core",
        "BlockingExtras",
        "TestSupport",
        "OtherExtras",
    )
    return {
        "headers": [
            {
                "component": component,
                "path": f"{index}.h",
                "visibility": "Public",
                "currentInstall": "installed",
            }
            for index, component in enumerate(components, start=1)
        ]
    }


def _write_stage(stage_dir: Path) -> None:
    for header in ("1.h", "2.h", "3.h", "4.h"):
        header_path = stage_dir / "include" / "qcurl" / header
        header_path.parent.mkdir(parents=True, exist_ok=True)
        header_path.write_text("// fixture\n", encoding="utf-8")
    library_dir = stage_dir / "lib"
    library_dir.mkdir(parents=True, exist_ok=True)
    (library_dir / "libQCurl.so").write_bytes(b"core")
    (library_dir / "libQCurlTestSupport.a").write_bytes(b"test-support")
    (library_dir / "libQCurlOtherExtras.so").write_bytes(b"extras")
    target_dir = library_dir / "cmake" / "QCurl"
    target_dir.mkdir(parents=True, exist_ok=True)
    (target_dir / "QCurlTargets.cmake").write_text("# fixture\n", encoding="utf-8")
    (target_dir / "QCurlBlockingExtrasTargets.cmake").write_text(
        "# fixture\n", encoding="utf-8"
    )
    (target_dir / "QCurlTestSupportTargets.cmake").write_text(
        "# fixture\n", encoding="utf-8"
    )
    (target_dir / "QCurlOtherExtrasTargets.cmake").write_text(
        "# fixture\n", encoding="utf-8"
    )


def test_install_inventory_requires_every_runtime_target(tmp_path: Path) -> None:
    stage_dir = tmp_path / "stage"
    stage_dir.mkdir()
    (stage_dir / "package.txt").write_text("fixture\n", encoding="utf-8")

    with pytest.raises(
        release_package_evidence.PackageEvidenceError,
        match="no files for targets",
    ):
        release_package_evidence._install_inventory(
            stage_dir,
            {spec.target: [] for spec in release_package_evidence.CONSUMERS},
        )


def test_install_inventory_rejects_blocking_runtime_library(tmp_path: Path) -> None:
    stage_dir = tmp_path / "stage"
    library_dir = stage_dir / "lib"
    library_dir.mkdir(parents=True)
    (library_dir / "libQCurlBlockingExtras.so").write_bytes(b"obsolete")

    with pytest.raises(
        release_package_evidence.PackageEvidenceError,
        match="independent runtime library",
    ):
        release_package_evidence._install_inventory(
            stage_dir,
            {spec.target: ["fixture.h"] for spec in release_package_evidence.CONSUMERS},
        )


def _write_producer_profile(tmp_path: Path, build_dir: Path) -> tuple[Path, Path]:
    profile = build_dir / "generated" / "consumer-profile.cmake"
    profile.parent.mkdir(parents=True)
    dependency = tmp_path / "producer-dependencies"
    dependency_lib = dependency / "lib"
    dependency_lib.mkdir(parents=True)
    for name in ("libQt6Core.so.6", "libcurl.so.4"):
        (dependency_lib / name).write_bytes(b"fixture")
    values = {
        "CMAKE_CXX_COMPILER": "/usr/bin/g++",
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_CXX_STANDARD": "17",
        "CMAKE_CXX_EXTENSIONS": "OFF",
        "CMAKE_PREFIX_PATH": str(dependency),
        "Qt6Core_DIR": str(dependency_lib / "cmake/Qt6Core"),
        "CURL_LIBRARY_RELEASE": str(dependency_lib / "libcurl.so.4"),
        "QCURL_PRODUCER_COMPILER_ID": "GNU",
        "QCURL_PRODUCER_COMPILER_VERSION": "16",
        "QCURL_PRODUCER_QT_VERSION": "6.10.3",
        "QCURL_PRODUCER_CURL_VERSION": "8.14.1",
        "QCURL_PRODUCER_GLIBCXX_USE_CXX11_ABI": "1",
        "QCURL_PRODUCER_VERSION": "2.1.0",
        "QCURL_PRODUCER_ABI_VERSION": "2.1",
        "QCURL_PRODUCER_SHARED": "ON",
    }
    profile.write_text(
        "".join(
            f'set({name} [=[{value}]=] CACHE STRING "Producer consumer profile" FORCE)\n'
            for name, value in values.items()
        ),
        encoding="utf-8",
    )
    return profile, dependency_lib


def _package_runner(build_dir: Path, dependency_lib: Path, commands: list):
    def fake_run(
        command: list[str], *, capture_output: bool, env: dict[str, str] | None = None
    ) -> subprocess.CompletedProcess[str]:
        del capture_output
        commands.append(command)
        if len(command) >= 2 and command[1] == "--install":
            prefix = Path(command[command.index("--prefix") + 1])
            _write_stage(prefix)
        if command[0] == "ldd":
            assert env is not None, "consumer inspection must bind the producer runtime"
            assert "LD_DEBUG" not in env
            assert str(dependency_lib) in env["LD_LIBRARY_PATH"].split(":")
            assert "/ambient/wrong-provider" not in env["LD_LIBRARY_PATH"]
            prefix = build_dir / "evidence/package-stage/shared/lib"
            output = f"libQCurl.so.2.1 => {prefix / 'libQCurl.so'} (0x123)\n"
            if "OtherExtras" in command[1]:
                output += f"libQCurlOtherExtras.so.2.1 => {prefix / 'libQCurlOtherExtras.so'} (0x456)\n"
            output += (
                f"libQt6Core.so.6 => {dependency_lib / 'libQt6Core.so.6'} (0x789)\n"
            )
            output += f"libcurl.so.4 => {dependency_lib / 'libcurl.so.4'} (0xabc)\n"
            return subprocess.CompletedProcess(command, 0, output, "")
        if command[0].startswith(str(build_dir / "evidence/package-consumers")):
            assert (
                env is not None
            ), "consumer execution must use its inspection environment"
            assert str(dependency_lib) in env["LD_LIBRARY_PATH"].split(":")
        return subprocess.CompletedProcess(command, 0, "", "")

    return fake_run


def _assert_install_evidence(
    install_report: Path,
    lifecycle_report: Path,
    build_dir: Path,
    profile: Path,
    commands: list,
) -> None:
    report = json.loads(install_report.read_text(encoding="utf-8"))
    assert report["schema"] == "qcurl/release-package-evidence@v1"
    assert report["linkage"] == "shared"
    assert report["stagePrefix"] == str(build_dir / "evidence/package-stage/shared")
    assert report["profile"] == {
        "path": str(profile),
        "sha256": release_package_evidence.file_sha256(profile),
    }
    assert len(report["consumers"]) == len(release_package_evidence.CONSUMERS)
    assert all(item["result"] == "pass" for item in report["consumers"])
    assert all(item["runtimeLibraries"] for item in report["consumers"])
    assert all(
        item["type"] == "file" and item["sha256"] and item["size"] > 0
        for item in report["installInventory"]["files"]
    )
    assert lifecycle_report.is_file()
    assert lifecycle_report.stat().st_size > 0
    assert (
        sum(len(command) > 1 and command[1] == "--install" for command in commands) == 1
    )
    assert (
        sum(len(command) > 1 and command[1] == "--build" for command in commands) == 5
    )
    configure_commands = [command for command in commands if "-S" in command]
    assert len(configure_commands) == 4
    assert all(
        command[command.index("-C") + 1] == str(profile)
        for command in configure_commands
    )
    assert all(
        "-DCMAKE_BUILD_TYPE=Release" not in command for command in configure_commands
    )


def test_run_evidence_writes_install_and_lifecycle_reports(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    repo_root = tmp_path / "repo"
    repo_root.mkdir()
    build_dir = tmp_path / "release-shared"
    profile, dependency_lib = _write_producer_profile(tmp_path, build_dir)
    monkeypatch.setenv("LD_LIBRARY_PATH", "/ambient/wrong-provider")
    contract_path = tmp_path / "package-contract.json"
    surface_path = tmp_path / "surface-manifest.json"
    install_report = tmp_path / "evidence" / "install.json"
    lifecycle_report = tmp_path / "evidence" / "lifecycle.xml"
    contract_path.write_text(json.dumps(_contract()), encoding="utf-8")
    surface_path.write_text(json.dumps(_surface_manifest()), encoding="utf-8")
    commands: list[list[str]] = []
    release_package_evidence.run_evidence(
        repo_root,
        build_dir,
        linkage="shared",
        contract_path=contract_path,
        surface_manifest=surface_path,
        install_report=install_report,
        lifecycle_report=lifecycle_report,
        cmake="cmake",
        jobs=2,
        run_command=_package_runner(build_dir, dependency_lib, commands),
    )
    _assert_install_evidence(
        install_report, lifecycle_report, build_dir, profile, commands
    )


@pytest.mark.parametrize("existing", ["package-stage/shared", "package-consumers"])
def test_package_evidence_preserves_existing_stage_or_consumer_build(
    tmp_path: Path, existing: str
) -> None:
    """Repeated capture cannot destroy prior installation or consumer evidence."""

    existing_path = tmp_path / "build/evidence" / existing
    existing_path.mkdir(parents=True)
    marker = existing_path / "retained.txt"
    marker.write_text("prior evidence", encoding="utf-8")
    contract = tmp_path / "contract.json"
    surface = tmp_path / "surface.json"
    contract.write_text(json.dumps(_contract()), encoding="utf-8")
    surface.write_text(json.dumps(_surface_manifest()), encoding="utf-8")
    with pytest.raises(
        release_package_evidence.PackageEvidenceError, match="refusing to overwrite"
    ):
        release_package_evidence.run_evidence(
            tmp_path,
            tmp_path / "build",
            linkage="shared",
            contract_path=contract,
            surface_manifest=surface,
            install_report=tmp_path / "install.json",
            lifecycle_report=tmp_path / "lifecycle.xml",
            cmake="cmake",
            jobs=1,
            run_command=lambda *args, **kwargs: pytest.fail(
                "must reject before running commands"
            ),
        )
    assert marker.read_text(encoding="utf-8") == "prior evidence"


def test_package_consumer_requires_explicit_producer_profile(tmp_path: Path) -> None:
    """Missing producer settings cannot silently select an ambient compiler."""

    with pytest.raises(
        release_package_evidence.PackageEvidenceError, match="missing producer"
    ):
        release_package_evidence._run_consumers(
            tmp_path,
            tmp_path / "build",
            tmp_path / "stage",
            cmake="cmake",
            jobs=1,
            linkage="shared",
            run_command=lambda *args, **kwargs: pytest.fail(
                "must reject before configure"
            ),
        )


@pytest.mark.parametrize(
    "linkage,output",
    [
        ("shared", "libQCurl.so.2.1 => /old/libQCurl.so.2.1 (0x123)"),
        ("shared", "libQCurl.so.2.1 => not found"),
        ("shared", ""),
        ("static", "libQCurl.so.2.1 => /old/libQCurl.so.2.1 (0x123)"),
    ],
)
def test_package_consumer_rejects_missing_or_foreign_runtime(
    tmp_path: Path, linkage: str, output: str
) -> None:
    """A successful process exit cannot hide a different loaded QCurl library."""

    with pytest.raises(release_package_evidence.CompatibilityError):
        release_package_evidence.consumer_runtime_libraries(
            tmp_path / "consumer",
            tmp_path / "stage",
            linkage=linkage,
            component="Core",
            profile={},
            run_command=lambda command, **kwargs: subprocess.CompletedProcess(
                command, 0, output, ""
            ),
        )


def test_run_evidence_rejects_wrong_linkage_consumer_contract(tmp_path: Path) -> None:
    contract_path = tmp_path / "contract.json"
    contract = _contract()
    delivery_targets = contract["deliveryTargets"]
    assert isinstance(delivery_targets, dict)
    core = delivery_targets["Core"]
    assert isinstance(core, dict)
    consumers = core["consumerTests"]
    assert isinstance(consumers, dict)
    consumers["static"] = "wrong-consumer"
    contract_path.write_text(json.dumps(contract), encoding="utf-8")

    with pytest.raises(
        release_package_evidence.PackageEvidenceError,
        match="consumer mismatch: Core.static",
    ):
        release_package_evidence.run_evidence(
            tmp_path / "repo",
            tmp_path / "build",
            linkage="static",
            contract_path=contract_path,
            surface_manifest=tmp_path / "surface.json",
            install_report=tmp_path / "install.json",
            lifecycle_report=tmp_path / "lifecycle.xml",
            cmake="cmake",
            jobs=1,
        )
