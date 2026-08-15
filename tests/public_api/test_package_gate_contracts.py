from __future__ import annotations

import json
from pathlib import Path
import subprocess

import pytest

from scripts import run_release_gate
from tests.public_api import package_gate_contracts
from tests.public_api import stage_contracts


def _contract_path() -> Path:
    return Path(__file__).with_name("package_gate_manifest.json")


def test_package_gate_manifest_covers_every_exported_runtime_target() -> None:
    contract = package_gate_contracts.load_contract(_contract_path())

    package_gate_contracts.validate_contract(contract)

    targets = contract["runtimeTargets"]
    assert set(targets) == {
        "Core",
        "BlockingExtras",
        "TestSupport",
        "OtherExtras",
    }
    for target in targets.values():
        assert set(target["consumerTests"]) == {"shared", "static"}
        assert target["lifecycleTests"]
        assert target["sanitizerEvidence"]


def test_package_gate_rejects_force_disabled_release_candidate(tmp_path: Path) -> None:
    build_dir = tmp_path / "build"
    (build_dir / "src").mkdir(parents=True)
    (build_dir / "CMakeCache.txt").write_text(
        "QCURL_FORCE_DISABLE_WEBSOCKET_SUPPORT:BOOL=ON\n",
        encoding="utf-8",
    )
    (build_dir / "src" / "QCurlConfig.h").write_text(
        '#define QCURL_LIBCURL_VERSION "8.5.0"\n'
        "/* QCURL_WEBSOCKET_SUPPORT is not enabled */\n",
        encoding="utf-8",
    )

    with pytest.raises(
        package_gate_contracts.PackageGateError,
        match="negative capability variant",
    ):
        package_gate_contracts.validate_release_candidate(build_dir)


def test_package_gate_accepts_default_websocket_capable_candidate(tmp_path: Path) -> None:
    build_dir = tmp_path / "build"
    (build_dir / "src").mkdir(parents=True)
    (build_dir / "CMakeCache.txt").write_text(
        "QCURL_FORCE_DISABLE_WEBSOCKET_SUPPORT:BOOL=OFF\n",
        encoding="utf-8",
    )
    (build_dir / "src" / "QCurlConfig.h").write_text(
        '#define QCURL_LIBCURL_VERSION "8.5.0"\n'
        "#define QCURL_WEBSOCKET_SUPPORT\n",
        encoding="utf-8",
    )

    candidate = package_gate_contracts.validate_release_candidate(build_dir)

    assert candidate.websocket_capable is True
    assert candidate.websocket_enabled is True
    assert candidate.force_disabled is False


def test_default_install_stage_omits_component_filter(tmp_path: Path) -> None:
    commands: list[list[str]] = []

    def run_command(command: list[str]):
        commands.append(command)

    result = stage_contracts.install_stage(
        type(
            "Args",
            (),
            {
                "stage_dir": tmp_path / "stage",
                "build_dir": tmp_path / "build",
                "cmake": "cmake",
                "config": "",
                "components": None,
                "all_components": True,
            },
        )(),
        run_command=run_command,
        fail_func=lambda _message: 1,
    )

    assert result == 0
    assert len(commands) == 1
    assert "--component" not in commands[0]


def test_install_inventory_records_every_file_and_target_owner(tmp_path: Path) -> None:
    stage_dir = tmp_path / "stage"
    include_dir = stage_dir / "include" / "qcurl"
    cmake_dir = stage_dir / "lib" / "cmake" / "QCurl"
    pkgconfig_dir = stage_dir / "lib" / "pkgconfig"
    include_dir.mkdir(parents=True)
    cmake_dir.mkdir(parents=True)
    pkgconfig_dir.mkdir(parents=True)

    for name in (
        "QCNetworkAccessManager.h",
        "QCBlockingNetworkClient.h",
        "QCNetworkTestSupport.h",
        "QCNetworkDiagnostics.h",
        "QCurlConfig.h",
    ):
        (include_dir / name).write_text("// fixture\n", encoding="utf-8")
    for name in ("libQCurl.so", "libQCurlOtherExtras.so"):
        (stage_dir / "lib" / name).write_bytes(b"fixture")
    (cmake_dir / "QCurlTargets.cmake").write_text("# fixture\n", encoding="utf-8")
    (cmake_dir / "QCurlConfig.cmake").write_text("# fixture\n", encoding="utf-8")
    (pkgconfig_dir / "qcurl.pc").write_text("# fixture\n", encoding="utf-8")
    (pkgconfig_dir / "qcurl-other-extras.pc").write_text("# fixture\n", encoding="utf-8")

    manifests = {
        "Core": ["QCNetworkAccessManager.h"],
        "BlockingExtras": ["QCBlockingNetworkClient.h"],
        "TestSupport": ["QCNetworkTestSupport.h"],
        "OtherExtras": ["QCNetworkDiagnostics.h"],
    }
    inventory = package_gate_contracts.build_install_inventory(stage_dir, manifests)

    paths = {entry["path"] for entry in inventory["files"]}
    assert paths == {
        path.relative_to(stage_dir).as_posix()
        for path in stage_dir.rglob("*")
        if path.is_file()
    }
    assert inventory["runtimeTargets"]["Core"]["files"]
    assert inventory["runtimeTargets"]["BlockingExtras"]["files"]
    assert inventory["runtimeTargets"]["TestSupport"]["files"]
    assert inventory["runtimeTargets"]["OtherExtras"]["files"]
    assert all(entry["owners"] for entry in inventory["files"])

    encoded = json.dumps(inventory)
    assert str(stage_dir) not in encoded


def test_lifecycle_gate_requires_websocket_and_pool_for_capable_candidate(tmp_path: Path) -> None:
    build_dir = tmp_path / "build"
    (build_dir / "src").mkdir(parents=True)
    (build_dir / "CMakeCache.txt").write_text(
        "QCURL_FORCE_DISABLE_WEBSOCKET_SUPPORT:BOOL=OFF\n",
        encoding="utf-8",
    )
    (build_dir / "src" / "QCurlConfig.h").write_text(
        '#define QCURL_LIBCURL_VERSION "8.5.0"\n'
        "#define QCURL_WEBSOCKET_SUPPORT\n",
        encoding="utf-8",
    )
    contract = package_gate_contracts.load_contract(_contract_path())
    registered = sorted(
        {
            name
            for target in contract["runtimeTargets"].values()
            for name in target["lifecycleTests"]
        }
    )
    commands: list[list[str]] = []
    report = tmp_path / "evidence" / "lifecycle.xml"

    def run_command(command: list[str], *, capture_output: bool):
        commands.append(command)
        if "--output-junit" in command:
            report.parent.mkdir(parents=True, exist_ok=True)
            report.write_text("<testsuite tests='1'/>\n", encoding="utf-8")
        stdout = json.dumps({"tests": [{"name": name} for name in registered]})
        return subprocess.CompletedProcess(
            command,
            0,
            stdout=stdout if capture_output else "",
            stderr="",
        )

    result = package_gate_contracts.run_lifecycle_gate(
        build_dir,
        contract,
        ctest="ctest",
        report=report,
        run_command=run_command,
    )

    assert result == 0
    assert "tst_QCWebSocket" in commands[1]
    assert "tst_QCWebSocketPool" in commands[1]
    assert "tst_QCWebSocket" in commands[2][-3]
    assert "tst_QCWebSocketPool" in commands[2][-3]
    assert commands[2][-2:] == ["--output-junit", str(report)]
    assert report.is_file()


def test_full_release_plan_gates_default_shared_and_static_packages(
    tmp_path: Path,
    capsys,
) -> None:
    trees = {
        "release-shared": tmp_path / "release-shared",
        "release-static": tmp_path / "release-static",
        "test-shared-gcc": tmp_path / "test-shared-gcc",
        "test-shared-clang": tmp_path / "test-shared-clang",
        "asan-ubsan-lsan": tmp_path / "asan-ubsan-lsan",
        "tsan": tmp_path / "tsan",
    }
    for tree_id, path in trees.items():
        build_testing = "OFF" if tree_id.startswith("release-") else "ON"
        shared_libs = "OFF" if tree_id == "release-static" else "ON"
        compiler = (
            "clang++"
            if tree_id in {"test-shared-clang", "asan-ubsan-lsan", "tsan"}
            else "g++"
        )
        sanitizer = tree_id if tree_id in {"asan-ubsan-lsan", "tsan"} else ""
        path.mkdir(parents=True)
        (path / "CMakeCache.txt").write_text(
            f"BUILD_TESTING:BOOL={build_testing}\n"
            f"QCURL_BUILD_SHARED_LIBS:BOOL={shared_libs}\n"
            f"CMAKE_CXX_COMPILER:FILEPATH={compiler}\n"
            f"QCURL_SANITIZER_PROFILE:STRING={sanitizer}\n",
            encoding="utf-8",
        )
    result = run_release_gate.main(
        [
            "--tier",
            "full",
            "--release-shared-build-dir",
            str(trees["release-shared"]),
            "--release-static-build-dir",
            str(trees["release-static"]),
            "--test-shared-gcc-build-dir",
            str(trees["test-shared-gcc"]),
            "--test-shared-clang-build-dir",
            str(trees["test-shared-clang"]),
            "--asan-ubsan-lsan-build-dir",
            str(trees["asan-ubsan-lsan"]),
            "--tsan-build-dir",
            str(trees["tsan"]),
            "--dry-run",
        ]
    )

    assert result == 0
    plan = json.loads(capsys.readouterr().out)
    steps = {step["name"]: step for step in plan["steps"]}
    names = [step["name"] for step in plan["steps"]]

    assert names.index("shared_package_candidate") < names.index("shared_package_evidence")
    assert names.index("static_configure") < names.index("static_package_candidate")
    assert names.index("static_package_candidate") < names.index("static_package_evidence")
    assert "scripts/release_package_evidence.py" in steps["shared_package_evidence"][
        "command"
    ]
    assert "scripts/release_package_evidence.py" in steps["static_package_evidence"][
        "command"
    ]
    assert "ctest" not in steps["shared_package_evidence"]["command"]
    assert "ctest" not in steps["static_package_evidence"]["command"]
    assert set(steps["shared_package_evidence"]["requiredArtifactIds"]) == {
        "shared_install_consumer_report",
        "shared_lifecycle_report",
    }
    assert set(steps["static_package_evidence"]["requiredArtifactIds"]) == {
        "static_install_consumer_report",
        "static_lifecycle_report",
    }
    assert "package_asan_ubsan_lsan" in steps
    assert "package_tsan" in steps
    assert "-DQCURL_FORCE_DISABLE_WEBSOCKET_SUPPORT=OFF" in steps["static_configure"][
        "command"
    ]
