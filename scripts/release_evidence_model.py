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
    "abi_current_report": ArtifactContract(
        "abi_current_report",
        "abi_current_baseline_diff",
        "release-shared",
        "text",
        "abi/qcurl-core-v2.abidiff.txt",
    ),
    "abi_current_snapshot": ArtifactContract(
        "abi_current_snapshot",
        "abi_current_baseline_diff",
        "release-shared",
        "xml",
        "abi/qcurl-core-v2.current.abi.xml",
    ),
    "abi_hardbreak_report": ArtifactContract(
        "abi_hardbreak_report",
        "abi_hardbreak_report",
        "release-shared",
        "text",
        "abi/qcurl-core-v1-to-v2.abidiff.txt",
    ),
    "abi_hardbreak_current_snapshot": ArtifactContract(
        "abi_hardbreak_current_snapshot",
        "abi_hardbreak_report",
        "release-shared",
        "xml",
        "abi/qcurl-core-v2.promotion-candidate.abi.xml",
    ),
    "core_dynamic_symbols": ArtifactContract(
        "core_dynamic_symbols",
        "dynamic_symbol_allowlist",
        "release-shared",
        "json",
        "abi/qcurl-core-v2.dynamic-symbols.json",
    ),
    "other_extras_dynamic_symbols": ArtifactContract(
        "other_extras_dynamic_symbols",
        "other_extras_dynamic_symbol_allowlist",
        "release-shared",
        "json",
        "abi/qcurl-other-extras-v2.dynamic-symbols.json",
    ),
    "capability_matrix_report": ArtifactContract(
        "capability_matrix_report",
        "capability_matrix_probe",
        "test-shared-gcc",
        "json",
        "libcurl_consistency/reports/capabilities.json",
    ),
}


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
