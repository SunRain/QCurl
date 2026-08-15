from __future__ import annotations

import json
import subprocess
from pathlib import Path

import pytest

from scripts import release_package_evidence


def _contract() -> dict[str, object]:
    return {
        "runtimeTargets": {
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
    layers = (
        "Core",
        "Blocking Extras",
        "Test Support",
        "Other Extras",
    )
    return {
        "headers": [
            {"layer": layer, "path": f"{index}.h"}
            for index, layer in enumerate(layers, start=1)
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
    (library_dir / "libQCurlOtherExtras.so").write_bytes(b"extras")
    target_dir = library_dir / "cmake" / "QCurl"
    target_dir.mkdir(parents=True, exist_ok=True)
    (target_dir / "QCurlTargets.cmake").write_text("# fixture\n", encoding="utf-8")


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


def test_run_evidence_writes_install_and_lifecycle_reports(tmp_path: Path) -> None:
    repo_root = tmp_path / "repo"
    repo_root.mkdir()
    build_dir = tmp_path / "release-shared"
    contract_path = tmp_path / "package-contract.json"
    surface_path = tmp_path / "surface-manifest.json"
    install_report = tmp_path / "evidence" / "install.json"
    lifecycle_report = tmp_path / "evidence" / "lifecycle.xml"
    contract_path.write_text(
        json.dumps(_contract()),
        encoding="utf-8",
    )
    surface_path.write_text(
        json.dumps(_surface_manifest()),
        encoding="utf-8",
    )
    commands: list[list[str]] = []

    def fake_run(
        command: list[str], *, capture_output: bool
    ) -> subprocess.CompletedProcess[str]:
        del capture_output
        commands.append(command)
        if len(command) >= 2 and command[1] == "--install":
            prefix = Path(command[command.index("--prefix") + 1])
            _write_stage(prefix)
        return subprocess.CompletedProcess(command, 0, "", "")

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
        run_command=fake_run,
    )

    report = json.loads(install_report.read_text(encoding="utf-8"))
    assert report["schema"] == "qcurl/release-package-evidence@v1"
    assert report["linkage"] == "shared"
    assert len(report["consumers"]) == len(release_package_evidence.CONSUMERS)
    assert all(item["result"] == "pass" for item in report["consumers"])
    assert lifecycle_report.is_file()
    assert lifecycle_report.stat().st_size > 0
    assert sum(len(command) > 1 and command[1] == "--install" for command in commands) == 1
    assert sum(len(command) > 1 and command[1] == "--build" for command in commands) == 5


def test_run_evidence_rejects_wrong_linkage_consumer_contract(tmp_path: Path) -> None:
    contract_path = tmp_path / "contract.json"
    contract = _contract()
    runtime_targets = contract["runtimeTargets"]
    assert isinstance(runtime_targets, dict)
    core = runtime_targets["Core"]
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
