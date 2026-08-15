#!/usr/bin/env python3
"""从 BUILD_TESTING=OFF 发行树生成安装、消费者和生命周期证据。"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from typing import Any
from typing import Callable


@dataclass(frozen=True)
class ConsumerSpec:
    """描述一个安装包消费者及其对应导出目标。"""

    target: str
    test_name: str
    source_dir: str
    executable: str


CONSUMERS = (
    ConsumerSpec(
        "Core",
        "qcurl_public_api_consumer_smoke",
        "consumer_smoke",
        "qcurl_public_api_consumer_smoke",
    ),
    ConsumerSpec(
        "BlockingExtras",
        "qcurl_public_api_blocking_extras_consumer_smoke",
        "consumer_blocking_extras_smoke",
        "qcurl_public_api_consumer_blocking_extras_smoke",
    ),
    ConsumerSpec(
        "TestSupport",
        "qcurl_public_api_test_support_consumer_smoke",
        "consumer_test_support_smoke",
        "qcurl_public_api_consumer_test_support_smoke",
    ),
    ConsumerSpec(
        "OtherExtras",
        "qcurl_public_api_other_extras_consumer_smoke",
        "consumer_other_extras_smoke",
        "qcurl_public_api_consumer_other_extras_smoke",
    ),
)

LAYER_TARGETS = {
    "Core": "Core",
    "Blocking Extras": "BlockingExtras",
    "Test Support": "TestSupport",
    "Other Extras": "OtherExtras",
}


class PackageEvidenceError(RuntimeError):
    """表示发行包证据无法完整生成。"""


RunCommand = Callable[..., subprocess.CompletedProcess[str]]


def _run_command(
    command: list[str],
    *,
    capture_output: bool,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        check=False,
        capture_output=capture_output,
        text=True,
    )


def _run_checked(
    command: list[str],
    *,
    run_command: RunCommand,
) -> None:
    completed = run_command(command, capture_output=True)
    if completed.returncode == 0:
        return
    details = completed.stderr.strip() or completed.stdout.strip()
    raise PackageEvidenceError(
        f"command failed ({completed.returncode}): {' '.join(command)}\n{details}"
    )


def _load_json_object(path: Path) -> dict[str, Any]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise PackageEvidenceError(f"JSON root must be an object: {path}")
    return payload


def _header_manifests(surface_manifest: Path) -> dict[str, list[str]]:
    payload = _load_json_object(surface_manifest)
    headers = payload.get("headers")
    if not isinstance(headers, list):
        raise PackageEvidenceError("surface manifest headers must be a list")

    manifests = {spec.target: [] for spec in CONSUMERS}
    for entry in headers:
        if not isinstance(entry, dict):
            raise PackageEvidenceError("surface manifest header must be an object")
        target = LAYER_TARGETS.get(entry.get("layer"))
        path = entry.get("path")
        if target is not None and isinstance(path, str) and path:
            manifests[target].append(path)
    if any(not paths for paths in manifests.values()):
        raise PackageEvidenceError("surface manifest must cover every runtime target")
    return manifests


def _validate_consumer_contract(contract: dict[str, Any], linkage: str) -> None:
    targets = contract.get("runtimeTargets")
    if not isinstance(targets, dict):
        raise PackageEvidenceError("package contract runtimeTargets must be an object")
    for spec in CONSUMERS:
        target = targets.get(spec.target)
        if not isinstance(target, dict):
            raise PackageEvidenceError(f"package contract is missing {spec.target}")
        consumers = target.get("consumerTests")
        if not isinstance(consumers, dict) or consumers.get(linkage) != spec.test_name:
            raise PackageEvidenceError(
                f"package contract consumer mismatch: {spec.target}.{linkage}"
            )


def _owners_for_path(path: str, manifests: dict[str, list[str]]) -> list[str]:
    name = Path(path).name
    if path.startswith("include/qcurl/"):
        if name == "QCurlConfig.h":
            return ["Core"]
        owners = [target for target, headers in manifests.items() if name in headers]
        return owners or ["Package"]
    if "libQCurlOtherExtras" in name or name == "qcurl-other-extras.pc":
        return ["OtherExtras"]
    if "libQCurl" in name or name == "qcurl.pc":
        return ["Core"]
    if name.startswith("QCurlTargets"):
        return [spec.target for spec in CONSUMERS]
    return ["Package"]


def _install_inventory(
    stage_dir: Path,
    manifests: dict[str, list[str]],
) -> dict[str, Any]:
    files = []
    target_files = {spec.target: [] for spec in CONSUMERS}
    for path in sorted(candidate for candidate in stage_dir.rglob("*") if candidate.is_file()):
        relative = path.relative_to(stage_dir).as_posix()
        owners = _owners_for_path(relative, manifests)
        files.append({"path": relative, "owners": owners})
        for owner in owners:
            if owner in target_files:
                target_files[owner].append(relative)
    if not files:
        raise PackageEvidenceError("default install tree is empty")
    missing = [target for target, paths in target_files.items() if not paths]
    if missing:
        raise PackageEvidenceError(
            "default install tree has no files for targets: " + ", ".join(missing)
        )
    return {
        "schema": "qcurl/package-install-inventory@v1",
        "files": files,
        "runtimeTargets": {
            target: {"files": paths} for target, paths in target_files.items()
        },
    }


def _prepare_stage(
    build_dir: Path,
    stage_dir: Path,
    *,
    cmake: str,
    jobs: int,
    run_command: RunCommand,
) -> None:
    if stage_dir.exists():
        shutil.rmtree(stage_dir)
    _run_checked(
        [cmake, "--build", str(build_dir), "--parallel", str(jobs)],
        run_command=run_command,
    )
    _run_checked(
        [cmake, "--install", str(build_dir), "--prefix", str(stage_dir)],
        run_command=run_command,
    )


def _run_consumers(
    repo_root: Path,
    build_dir: Path,
    stage_dir: Path,
    *,
    cmake: str,
    jobs: int,
    run_command: RunCommand,
) -> list[dict[str, str]]:
    results = []
    consumer_root = build_dir / "evidence" / "package-consumers"
    for spec in CONSUMERS:
        consumer_build = consumer_root / spec.target
        if consumer_build.exists():
            shutil.rmtree(consumer_build)
        source_dir = repo_root / "tests" / "public_api" / spec.source_dir
        _run_checked(
            [
                cmake,
                "-S",
                str(source_dir),
                "-B",
                str(consumer_build),
                "-DCMAKE_BUILD_TYPE=Release",
                f"-DQCURL_STAGE_PREFIX={stage_dir}",
            ],
            run_command=run_command,
        )
        _run_checked(
            [cmake, "--build", str(consumer_build), "--parallel", str(jobs)],
            run_command=run_command,
        )
        _run_checked(
            [str(consumer_build / spec.executable)],
            run_command=run_command,
        )
        results.append(
            {"target": spec.target, "consumer": spec.test_name, "result": "pass"}
        )
    return results


def _write_lifecycle_report(
    path: Path,
    linkage: str,
    consumers: list[dict[str, str]],
) -> None:
    suite = ET.Element(
        "testsuite",
        name=f"qcurl-{linkage}-package-lifecycle",
        tests=str(len(consumers)),
        failures="0",
    )
    for result in consumers:
        ET.SubElement(
            suite,
            "testcase",
            classname=result["target"],
            name=result["consumer"],
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    ET.ElementTree(suite).write(path, encoding="utf-8", xml_declaration=True)


def run_evidence(
    repo_root: Path,
    build_dir: Path,
    *,
    linkage: str,
    contract_path: Path,
    surface_manifest: Path,
    install_report: Path,
    lifecycle_report: Path,
    cmake: str,
    jobs: int,
    run_command: RunCommand = _run_command,
) -> None:
    """生成绑定单一 OFF producer tree 的完整 package 证据。"""

    contract = _load_json_object(contract_path)
    _validate_consumer_contract(contract, linkage)
    manifests = _header_manifests(surface_manifest)
    stage_dir = build_dir / "evidence" / "package-stage" / linkage
    _prepare_stage(
        build_dir,
        stage_dir,
        cmake=cmake,
        jobs=jobs,
        run_command=run_command,
    )
    inventory = _install_inventory(stage_dir, manifests)
    consumers = _run_consumers(
        repo_root,
        build_dir,
        stage_dir,
        cmake=cmake,
        jobs=jobs,
        run_command=run_command,
    )
    install_report.parent.mkdir(parents=True, exist_ok=True)
    install_report.write_text(
        json.dumps(
            {
                "schema": "qcurl/release-package-evidence@v1",
                "linkage": linkage,
                "installInventory": inventory,
                "consumers": consumers,
            },
            ensure_ascii=False,
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    _write_lifecycle_report(lifecycle_report, linkage, consumers)


def main(argv: list[str] | None = None) -> int:
    """解析命令行并生成发行包证据。"""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--linkage", choices=("shared", "static"), required=True)
    parser.add_argument("--contract", type=Path, required=True)
    parser.add_argument("--surface-manifest", type=Path, required=True)
    parser.add_argument("--install-report", type=Path, required=True)
    parser.add_argument("--lifecycle-report", type=Path, required=True)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--jobs", type=int, default=1)
    args = parser.parse_args(argv)
    repo_root = Path(__file__).resolve().parent.parent
    try:
        run_evidence(
            repo_root,
            args.build_dir.resolve(),
            linkage=args.linkage,
            contract_path=args.contract.resolve(),
            surface_manifest=args.surface_manifest.resolve(),
            install_report=args.install_report.resolve(),
            lifecycle_report=args.lifecycle_report.resolve(),
            cmake=args.cmake,
            jobs=args.jobs,
        )
    except (json.JSONDecodeError, OSError, PackageEvidenceError) as exc:
        print(f"[release_package_evidence] {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
