"""UCE 的 CTest acceptance 执行与证据登记。"""

from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path
from typing import Any

from scripts.ctest_strict import ctest_targets_without_unique_pass
from scripts.uce.manifest import add_artifact
from scripts.uce.manifest import add_contract
from scripts.uce.manifest import add_policy_violation
from scripts.uce.manifest import add_result
from scripts.uce_gate.runtime import GateResult
from scripts.uce_gate.runtime import run_gate


@dataclass(frozen=True)
class _CtestSelection:
    gate_id: str
    list_id: str
    label: str
    artifact_prefix: str
    strict: bool
    policy_code: str


_OFFLINE = _CtestSelection(
    "ctest_strict_offline", "ctest_list_offline", "offline", "ctest_offline", True,
    "gate_offline_failed",
)
_ENV = _CtestSelection(
    "ctest_strict_env", "ctest_list_env", "env", "ctest_env", True, "gate_env_failed",
)
_PUBLIC_API_SLOW = _CtestSelection(
    "public_api_slow", "public_api_slow_list", "public-api-slow", "public_api_slow", False,
    "gate_public_api_slow_failed",
)
_CAPABILITY = _CtestSelection(
    "capability", "capability_list", "capability", "capability", True, "gate_capability_failed",
)


def _execution_command(repo_root: Path, build_dir: Path, selection: _CtestSelection) -> list[str]:
    label_pattern = f"^{selection.label}$"
    if selection.strict:
        return [
            "python3",
            str(repo_root / "scripts" / "ctest_strict.py"),
            "--build-dir",
            str(build_dir),
            "--label-regex",
            label_pattern,
            "--max-skips",
            "0",
        ]
    return [
        "ctest",
        "--test-dir",
        str(build_dir),
        "--output-on-failure",
        "--verbose",
        "--no-tests=error",
        "-L",
        label_pattern,
    ]


def _ctest_execution_errors(results: list[GateResult]) -> list[str]:
    """进程成功后，核对本次标签选中的非空目标集合是否全部实际通过。"""

    if any(result.returncode != 0 for result in results):
        return []
    try:
        listing = json.loads(results[0].log_path.read_text(encoding="utf-8"))
        names = [test["name"] for test in listing["tests"]]
        output = results[1].log_path.read_text(encoding="utf-8")
    except (OSError, ValueError, KeyError, TypeError) as exc:
        return [f"无法核对 CTest 目标清单与执行结果: {exc}"]
    if not names or any(not isinstance(name, str) or not name for name in names):
        return ["CTest 必需目标集合为空或名称无效"]
    if len(names) != len(set(names)):
        return ["CTest 必需目标集合包含重复名称"]
    incomplete = ctest_targets_without_unique_pass(output, names)
    if incomplete:
        return ["CTest 必需目标缺少唯一的 Passed 执行结果: " + ", ".join(incomplete)]
    return []


def _register_ctest_evidence(
    manifest: dict[str, Any],
    selection: _CtestSelection,
    results: list[GateResult],
    evidence_dir: Path,
) -> None:
    execution_errors = _ctest_execution_errors(results)
    for result, suffix, kind in zip(results, ("list", "log"), ("report", "log")):
        errors = execution_errors if suffix == "log" else []
        add_result(
            manifest,
            result_id=result.gate_id,
            kind="gate",
            result="fail" if result.returncode != 0 or errors else "pass",
            returncode=result.returncode,
            duration_s=result.duration_s,
            log_file=str(result.log_path),
            details={"execution_errors": errors} if errors else None,
        )
        add_artifact(
            manifest,
            artifact_id=f"{selection.artifact_prefix}_{suffix}",
            path=str(result.log_path.relative_to(evidence_dir)),
            kind=kind,
            required=True,
        )
    failed = any(result.returncode != 0 for result in results) or bool(execution_errors)
    contract_name = selection.label.replace("-", "_")
    add_contract(
        manifest,
        contract_id=f"qtest_{contract_name}@v1",
        provider="ctest_strict" if selection.strict else "ctest",
        result="fail" if failed else "pass",
        required=True,
        report_artifact=f"{selection.artifact_prefix}_log",
        notes=[
            f"list_returncode={results[0].returncode}",
            f"gate_returncode={results[1].returncode}",
            *execution_errors,
        ],
    )
    if failed:
        add_policy_violation(manifest, selection.policy_code)


def _run_ctest_gate(
    repo_root: Path,
    build_dir: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
    selection: _CtestSelection,
    runtime_env: dict[str, str] | None = None,
) -> list[GateResult]:
    list_result = run_gate(
        selection.list_id,
        ["ctest", "--show-only=json-v1", "--no-tests=error", "-L", f"^{selection.label}$"],
        evidence_dir / "meta" / f"ctest_list_{selection.label.replace('-', '_')}.json",
        cwd=build_dir,
        env=runtime_env,
    )
    gate_result = run_gate(
        selection.gate_id,
        _execution_command(repo_root, build_dir, selection),
        evidence_dir / "logs" / f"{selection.gate_id}.log",
        cwd=repo_root,
        env=runtime_env,
    )
    results = [list_result, gate_result]
    _register_ctest_evidence(manifest, selection, results, evidence_dir)
    return results


def run_offline_ctest_gate(
    repo_root: Path,
    build_dir: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
) -> list[GateResult]:
    """执行 offline 门禁，列表失败、测试失败和跳过均阻断。"""

    return _run_ctest_gate(repo_root, build_dir, evidence_dir, manifest, _OFFLINE)


def run_env_ctest_gate(
    repo_root: Path,
    build_dir: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
    runtime_env: dict[str, str],
) -> list[GateResult]:
    """在调用方提供的完整环境中执行 env 严格门禁。"""

    return _run_ctest_gate(repo_root, build_dir, evidence_dir, manifest, _ENV, runtime_env)


def run_public_api_slow_gate(
    repo_root: Path,
    build_dir: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
) -> list[GateResult]:
    """执行 public-api-slow，列表或命令失败及空集合均阻断。"""

    return _run_ctest_gate(repo_root, build_dir, evidence_dir, manifest, _PUBLIC_API_SLOW)


def run_capability_gate(
    repo_root: Path,
    build_dir: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
) -> list[GateResult]:
    """执行 capability 门禁，任何 QtTest 跳过均视为失败。"""

    return _run_ctest_gate(repo_root, build_dir, evidence_dir, manifest, _CAPABILITY)
