"""HTTPBin environment gate helpers for scripts/run_uce_gate.py."""

from __future__ import annotations

from pathlib import Path
from typing import Any
import os

from scripts.uce.manifest import add_artifact
from scripts.uce.manifest import add_contract
from scripts.uce_gate.runtime import GateResult
from scripts.uce_gate.runtime import parse_shell_exports
from scripts.uce_gate.runtime import record_gate_result
from scripts.uce_gate.runtime import run_gate
from scripts.uce_gate.runtime import write_text


def _register_httpbin_artifacts(manifest: dict[str, Any]) -> None:
    add_artifact(manifest, artifact_id="httpbin_start_log", path="logs/httpbin_start.log", kind="log", required=True)
    add_artifact(manifest, artifact_id="httpbin_stop_log", path="logs/httpbin_stop.log", kind="log", required=True)
    add_artifact(manifest, artifact_id="httpbin_env", path="httpbin/httpbin.env", kind="metadata", required=True)
    add_artifact(manifest, artifact_id="ctest_env_list", path="meta/ctest_list_env.txt", kind="report", required=True)
    add_artifact(manifest, artifact_id="ctest_env_log", path="logs/ctest_strict_env.log", kind="log", required=True)
    add_contract(
        manifest,
        contract_id="qtest_env@v1",
        provider="ctest_strict",
        result="fail",
        required=True,
        notes=["env gate 尚未获得有效 httpbin 环境时默认视为失败"],
    )


def _start_httpbin(repo_root: Path, env_file: Path, log_path: Path, manifest: dict[str, Any]) -> GateResult:
    result = run_gate(
        "httpbin_start",
        [
            str(repo_root / "tests" / "qcurl" / "httpbin" / "start_httpbin.sh"),
            "--write-env",
            str(env_file),
        ],
        log_path,
        cwd=repo_root,
    )
    record_gate_result(manifest, result)
    return result


def _load_httpbin_env(env_file: Path, httpbin_dir: Path, manifest: dict[str, Any]) -> tuple[dict[str, str], list[str]]:
    if not env_file.exists():
        return {}, ["env_preflight_httpbin_env_missing"]

    try:
        return parse_shell_exports(env_file), []
    except Exception as exc:
        write_text(httpbin_dir / "httpbin_env_parse_error.txt", f"{exc}\n")
        add_artifact(
            manifest,
            artifact_id="httpbin_env_parse_error",
            path="httpbin/httpbin_env_parse_error.txt",
            kind="report",
            required=True,
        )
        return {}, ["env_preflight_httpbin_env_parse_error"]


def _run_env_ctest_gates(
    repo_root: Path,
    build_dir: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
    env_values: dict[str, str],
) -> tuple[list[GateResult], list[str]]:
    logs_dir = evidence_dir / "logs"
    meta_dir = evidence_dir / "meta"
    env_for_ctest = os.environ.copy()
    env_for_ctest.update(env_values)

    list_result = run_gate(
        "ctest_list_env",
        ["ctest", "-N", "--no-tests=error", "-L", "env"],
        meta_dir / "ctest_list_env.txt",
        cwd=build_dir,
        env=env_for_ctest,
    )
    gate_result = run_gate(
        "ctest_strict_env",
        [
            "python3",
            str(repo_root / "scripts" / "ctest_strict.py"),
            "--build-dir",
            str(build_dir),
            "--label-regex",
            "env",
            "--max-skips",
            "0",
        ],
        logs_dir / "ctest_strict_env.log",
        cwd=repo_root,
        env=env_for_ctest,
    )
    for result in (list_result, gate_result):
        record_gate_result(manifest, result)

    add_contract(
        manifest,
        contract_id="qtest_env@v1",
        provider="ctest_strict",
        result="pass" if gate_result.returncode == 0 else "fail",
        required=True,
        report_artifact="ctest_env_log",
    )
    violations = [] if gate_result.returncode == 0 else ["gate_env_failed"]
    return [list_result, gate_result], violations


def _write_httpbin_unavailable(httpbin_dir: Path, manifest: dict[str, Any]) -> None:
    write_text(
        httpbin_dir / "httpbin_unavailable.txt",
        "httpbin 未就绪，无法运行 LABELS=env 证据门禁。\n",
    )
    add_artifact(
        manifest,
        artifact_id="httpbin_unavailable",
        path="httpbin/httpbin_unavailable.txt",
        kind="report",
        required=True,
    )


def _stop_httpbin(repo_root: Path, container_name: str, log_path: Path, manifest: dict[str, Any]) -> GateResult:
    command = [str(repo_root / "tests" / "qcurl" / "httpbin" / "stop_httpbin.sh")]
    if container_name:
        command.extend(["--name", container_name])
    result = run_gate("httpbin_stop", command, log_path, cwd=repo_root)
    record_gate_result(manifest, result)
    return result


def run_httpbin_gate(
    repo_root: Path,
    build_dir: Path,
    evidence_dir: Path,
    manifest: dict[str, Any],
) -> tuple[dict[str, str], list[GateResult], list[str]]:
    """Start httpbin, run the env gate when available, and stop httpbin."""

    httpbin_dir = evidence_dir / "httpbin"
    env_file = httpbin_dir / "httpbin.env"
    _register_httpbin_artifacts(manifest)

    results: list[GateResult] = []
    violations: list[str] = []
    start_result = _start_httpbin(repo_root, env_file, evidence_dir / "logs" / "httpbin_start.log", manifest)
    results.append(start_result)
    if start_result.returncode != 0:
        violations.append("env_preflight_httpbin_start_failed")

    env_values, env_violations = _load_httpbin_env(env_file, httpbin_dir, manifest)
    violations.extend(env_violations)

    if env_values.get("QCURL_HTTPBIN_URL"):
        env_results, gate_violations = _run_env_ctest_gates(repo_root, build_dir, evidence_dir, manifest, env_values)
        results.extend(env_results)
        violations.extend(gate_violations)
    else:
        violations.append("env_preflight_httpbin_url_missing")
        _write_httpbin_unavailable(httpbin_dir, manifest)

    stop_result = _stop_httpbin(
        repo_root,
        env_values.get("QCURL_HTTPBIN_CONTAINER_NAME", ""),
        evidence_dir / "logs" / "httpbin_stop.log",
        manifest,
    )
    results.append(stop_result)
    return env_values, results, violations
