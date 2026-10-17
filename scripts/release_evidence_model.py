"""release-required artifact 的固定 producer 合同。"""

from __future__ import annotations

import hashlib
import json
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class ArtifactContract:
    """描述一个稳定 artifact ID 的 gate、tree、kind 和相对路径。"""

    artifact_id: str
    gate: str
    tree_id: str
    kind: str
    relative_path: str


ARTIFACT_CONTRACTS = {
    "full_ctest_report": ArtifactContract(
        "full_ctest_report",
        "full_ctest",
        "test-shared-gcc",
        "junit",
        "evidence/full-ctest.xml",
    ),
    "clang_ctest_report": ArtifactContract(
        "clang_ctest_report",
        "clang_ctest",
        "test-shared-clang",
        "junit",
        "evidence/clang-ctest.xml",
    ),
    "parity_report": ArtifactContract(
        "parity_report",
        "libcurl_consistency_full",
        "test-shared-gcc",
        "json",
        "libcurl_consistency/reports/summary.json",
    ),
    "shared_install_consumer_report": ArtifactContract(
        "shared_install_consumer_report",
        "shared_package_evidence",
        "release-shared",
        "json",
        "evidence/package/shared-install-consumer.json",
    ),
    "static_install_consumer_report": ArtifactContract(
        "static_install_consumer_report",
        "static_package_evidence",
        "release-static",
        "json",
        "evidence/package/static-install-consumer.json",
    ),
    "shared_lifecycle_report": ArtifactContract(
        "shared_lifecycle_report",
        "shared_package_evidence",
        "release-shared",
        "junit",
        "evidence/lifecycle/shared.xml",
    ),
    "static_lifecycle_report": ArtifactContract(
        "static_lifecycle_report",
        "static_package_evidence",
        "release-static",
        "junit",
        "evidence/lifecycle/static.xml",
    ),
    "asan_ubsan_lsan_report": ArtifactContract(
        "asan_ubsan_lsan_report",
        "package_asan_ubsan_lsan",
        "asan-ubsan-lsan",
        "json",
        "evidence/package-sanitizers/asan-ubsan-lsan/report.json",
    ),
    "uce_report": ArtifactContract(
        "uce_report",
        "uce_evidence",
        "test-shared-gcc",
        "json",
        "evidence/uce/release-gate/manifest.json",
    ),
    "doxygen_report": ArtifactContract(
        "doxygen_report",
        "doxygen_report",
        "release-shared",
        "html",
        "evidence/doxygen/index.html",
    ),
    "core_dynamic_symbols": ArtifactContract(
        "core_dynamic_symbols",
        "dynamic_symbol_allowlist",
        "release-shared",
        "json",
        "abi/core.dynamic-symbols.json",
    ),
    "other_extras_dynamic_symbols": ArtifactContract(
        "other_extras_dynamic_symbols",
        "other_extras_dynamic_symbol_allowlist",
        "release-shared",
        "json",
        "abi/other-extras.dynamic-symbols.json",
    ),
    "capability_matrix_report": ArtifactContract(
        "capability_matrix_report",
        "capability_matrix_probe",
        "test-shared-gcc",
        "json",
        "libcurl_consistency/reports/capabilities.json",
    ),
}


for _artifact_id, _kind, _path in (
    ("compatibility_reference", "json", "reference.json"),
    ("compatibility_reference_validation", "json", "validation.json"),
    ("abi_core_snapshot", "xml", "abi/core.snapshot.xml"),
    ("abi_other_extras_snapshot", "xml", "abi/other-extras.snapshot.xml"),
):
    ARTIFACT_CONTRACTS[_artifact_id] = ArtifactContract(
        _artifact_id,
        "compatibility_reference_capture",
        "release-shared",
        _kind,
        f"evidence/compatibility/reference/{_path}",
    )

for _role in ("roundtrip", "first", "latest"):
    _gate = (
        "compatibility_reference_roundtrip"
        if _role == "roundtrip"
        else f"compatibility_{_role}_check"
    )
    _directory = "roundtrip" if _role == "roundtrip" else f"check-{_role}"
    for _name, _kind, _path in (
        ("report", "json", "report.json"),
        ("core_full", "text", "abi/core.full.txt"),
        ("core_residual", "text", "abi/core.residual.txt"),
        ("core_comparison", "json", "abi/core.comparison.json"),
        ("other_extras_full", "text", "abi/other-extras.full.txt"),
        ("other_extras_residual", "text", "abi/other-extras.residual.txt"),
        ("other_extras_comparison", "json", "abi/other-extras.comparison.json"),
    ):
        _artifact_id = f"compatibility_{_role}_{_name}"
        ARTIFACT_CONTRACTS[_artifact_id] = ArtifactContract(
            _artifact_id,
            _gate,
            "release-shared",
            _kind,
            f"evidence/compatibility/{_directory}/{_path}",
        )
    if _role == "roundtrip":
        continue
    _artifact_id = f"compatibility_{_role}_source"
    ARTIFACT_CONTRACTS[_artifact_id] = ArtifactContract(
        _artifact_id,
        _artifact_id,
        "release-shared",
        "json",
        f"evidence/compatibility/source-{_role}/report.json",
    )


GATE_REQUIRED_ARTIFACTS = {
    gate: tuple(
        contract.artifact_id
        for contract in ARTIFACT_CONTRACTS.values()
        if contract.gate == gate
    )
    for gate in {contract.gate for contract in ARTIFACT_CONTRACTS.values()}
}


def command_digest(command: object) -> str:
    """计算规范化 argv 的 SHA-256 摘要。"""

    payload = json.dumps(
        command, ensure_ascii=False, sort_keys=False, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def required_artifact_ids(gates: object) -> set[str]:
    """根据 required gate 重算固定 artifact ID 集合。"""

    if not isinstance(gates, list):
        return set()
    return {
        artifact_id
        for gate in gates
        for artifact_id in GATE_REQUIRED_ARTIFACTS.get(gate, ())
    }


def artifact_schema_valid(path: Path, kind: str) -> bool:
    """验证 required artifact 的内容类型，拒绝仅靠扩展名伪造的文件。"""

    try:
        if kind == "json":
            return isinstance(json.loads(path.read_text(encoding="utf-8")), dict)
        if kind in {"junit", "xml"}:
            root = ET.parse(path).getroot()
            if kind == "xml":
                return True
            tag = root.tag.rsplit("}", maxsplit=1)[-1]
            return tag in {"testsuite", "testsuites"}
        if kind == "html":
            prefix = path.read_text(encoding="utf-8", errors="replace")[:4096].lower()
            return "<html" in prefix or "<!doctype html" in prefix
        return kind == "text"
    except (ET.ParseError, OSError, UnicodeDecodeError, json.JSONDecodeError):
        return False
