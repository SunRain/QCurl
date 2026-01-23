#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
“基本无问题”验收门禁（强制归档工件）

门禁组合（全部必须通过）：
1) QtTest 证据门禁：LABELS=offline（skip=fail，max-skips=0）
2) QtTest 证据门禁：LABELS=env（依赖本地 httpbin + 本机端口 + node；skip=fail，max-skips=0）
3) QCurl↔libcurl 可观测一致性门禁：run_gate.py --suite p0 --build（policy：no skipped/no_tests/schema/redaction）

工件归档：
- 生成 evidence 目录（含 manifest.json、门禁日志、httpbin env、gate reports、可用的 test-artifacts 等）
- 生成 tar.gz（CI 侧强制上传；缺失即失败）
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, List, Optional


def _utc_now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def _safe_mkdir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def _write_text(path: Path, text: str) -> None:
    _safe_mkdir(path.parent)
    path.write_text(text, encoding="utf-8")


def _write_json(path: Path, payload: Dict) -> None:
    _safe_mkdir(path.parent)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def _best_effort_copytree(src: Path, dst: Path) -> bool:
    if not src.exists():
        return False
    _safe_mkdir(dst.parent)
    shutil.copytree(src, dst, dirs_exist_ok=True)
    return True


def _best_effort_copy_glob(src_root: Path, pattern: str, dst_root: Path) -> int:
    if not src_root.exists():
        return 0
    n = 0
    for p in src_root.glob(pattern):
        if not p.is_file():
            continue
        rel = p.relative_to(src_root)
        out = dst_root / rel
        _safe_mkdir(out.parent)
        shutil.copy2(p, out)
        n += 1
    return n


def _run_capture(cmd: List[str], cwd: Optional[Path] = None, env: Optional[Dict[str, str]] = None) -> subprocess.CompletedProcess:
    return subprocess.run(
        cmd,
        cwd=str(cwd) if cwd else None,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


@dataclass
class GateResult:
    name: str
    command: List[str]
    returncode: int
    duration_s: float
    log_file: str


_EXPORT_RE = re.compile(r'^\s*export\s+(?P<k>[A-Za-z_][A-Za-z0-9_]*)=(?P<v>.*)\s*$')


def _parse_shell_exports(env_file: Path) -> Dict[str, str]:
    env: Dict[str, str] = {}
    for raw in env_file.read_text(encoding="utf-8", errors="replace").splitlines():
        m = _EXPORT_RE.match(raw)
        if not m:
            continue
        k = m.group("k")
        v = m.group("v").strip()
        if (len(v) >= 2) and ((v[0] == v[-1]) and v[0] in ("'", '"')):
            v = v[1:-1]
        env[k] = v
    return env


def _tar_gz_dir(src_dir: Path, out_tgz: Path) -> None:
    _safe_mkdir(out_tgz.parent)
    with tarfile.open(out_tgz, "w:gz") as tf:
        for p in sorted(src_dir.rglob("*")):
            if p.is_dir():
                continue
            tf.add(p, arcname=str(p.relative_to(src_dir.parent)))


def _collect_versions(repo_root: Path) -> str:
    cmds = [
        ["uname", "-a"],
        ["git", "rev-parse", "HEAD"],
        ["cmake", "--version"],
        ["ctest", "--version"],
        ["ninja", "--version"],
        ["python3", "--version"],
        ["python3", "-m", "pip", "--version"],
        ["node", "--version"],
        ["curl", "--version"],
        ["docker", "--version"],
    ]
    out: List[str] = []
    out.append(f"# generated_at_utc: {_utc_now_iso()}\n")
    out.append(f"# platform: {platform.platform()}\n")
    out.append("\n")
    for cmd in cmds:
        try:
            rc = _run_capture(cmd, cwd=repo_root)
            out.append(f"$ {' '.join(cmd)}\n")
            out.append(f"returncode: {rc.returncode}\n")
            if (rc.stdout or "").strip():
                out.append(rc.stdout.rstrip() + "\n")
            out.append("\n")
        except Exception as exc:
            out.append(f"$ {' '.join(cmd)}\n")
            out.append(f"exception: {exc}\n\n")
    return "".join(out)


def _run_gate(
    name: str,
    cmd: List[str],
    log_path: Path,
    cwd: Optional[Path] = None,
    env: Optional[Dict[str, str]] = None,
) -> GateResult:
    started = time.time()
    rc = _run_capture(cmd, cwd=cwd, env=env)
    duration_s = round(time.time() - started, 3)
    _write_text(log_path, rc.stdout or "")
    return GateResult(
        name=name,
        command=cmd,
        returncode=int(rc.returncode),
        duration_s=float(duration_s),
        log_file=str(log_path),
    )


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description="Run 'basic no problem' acceptance gates and archive artifacts.")
    parser.add_argument(
        "--build-dir",
        default=os.environ.get("QCURL_BUILD_DIR", "build"),
        help="CMake build directory (default: build/ or $QCURL_BUILD_DIR).",
    )
    parser.add_argument(
        "--run-id",
        default=os.environ.get("QCURL_BASIC_GATE_RUN_ID", ""),
        help="Run identifier used in evidence dir name (default: $QCURL_BASIC_GATE_RUN_ID or UTC timestamp).",
    )
    parser.add_argument(
        "--evidence-root",
        default=os.environ.get("QCURL_EVIDENCE_ROOT", ""),
        help="Evidence root directory (default: <build-dir>/evidence/basic-no-problem).",
    )
    args = parser.parse_args(argv)

    repo_root = Path(__file__).resolve().parent.parent
    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = (repo_root / build_dir).resolve()

    run_id = (args.run_id or "").strip() or datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")

    if args.evidence_root:
        evidence_root = Path(args.evidence_root)
        if not evidence_root.is_absolute():
            evidence_root = (repo_root / evidence_root).resolve()
    else:
        evidence_root = build_dir / "evidence" / "basic-no-problem"

    evidence_dir = evidence_root / run_id
    logs_dir = evidence_dir / "logs"
    meta_dir = evidence_dir / "meta"
    httpbin_dir = evidence_dir / "httpbin"
    lc_dir = evidence_dir / "libcurl_consistency"

    _safe_mkdir(logs_dir)
    _safe_mkdir(meta_dir)
    _safe_mkdir(httpbin_dir)
    _safe_mkdir(lc_dir)

    manifest_path = evidence_dir / "manifest.json"
    tar_path = evidence_root / f"{run_id}.tar.gz"

    manifest: Dict[str, object] = {
        "gate_id": "basic-no-problem",
        "run_id": run_id,
        "generated_at_utc": _utc_now_iso(),
        "repo_root": str(repo_root),
        "build_dir": str(build_dir),
        "evidence_dir": str(evidence_dir),
        "tar_gz": str(tar_path),
        "results": [],
        "artifacts": {},
        # 机器可判定的“强口径违例”（类似 libcurl_consistency gate 的 policy_violations）：
        # - 为空：表示所有必须证据集合均已真实执行且满足口径
        # - 非空：即使某些步骤表面 returncode=0，也不得给出“基本无问题=通过”的结论
        "policy_violations": [],
        "environment": {
            "platform": platform.platform(),
            "python": sys.version,
        },
    }
    _write_json(manifest_path, manifest)

    # 供应链/复跑证据：版本快照（best-effort）
    versions_txt = meta_dir / "versions.txt"
    _write_text(versions_txt, _collect_versions(repo_root))

    results: List[GateResult] = []
    ok = True
    policy_violations: List[str] = []
    httpbin_env: Dict[str, str] = {}
    httpbin_env_file = httpbin_dir / "httpbin.env"
    lc_reports_dir = lc_dir / "reports"

    def add_policy_violation(code: str) -> None:
        if code not in policy_violations:
            policy_violations.append(code)

    try:
        # Gate 1: QtTest offline
        results.append(
            _run_gate(
                "ctest_list_offline",
                ["ctest", "-N", "--no-tests=error", "-L", "offline"],
                meta_dir / "ctest_list_offline.txt",
                cwd=build_dir,
            )
        )
        results.append(
            _run_gate(
                "ctest_strict_offline",
                [
                    "python3",
                    str(repo_root / "scripts" / "ctest_strict.py"),
                    "--build-dir",
                    str(build_dir),
                    "--label-regex",
                    "offline",
                    "--max-skips",
                    "0",
                ],
                logs_dir / "ctest_strict_offline.log",
                cwd=repo_root,
            )
        )
        if results[-1].returncode != 0:
            ok = False
            add_policy_violation("gate_offline_failed")

        # Gate 2: QtTest env (httpbin required)
        httpbin_start_log = logs_dir / "httpbin_start.log"
        httpbin_stop_log = logs_dir / "httpbin_stop.log"
        container_name = ""
        httpbin_started = False
        try:
            start_res = _run_gate(
                "httpbin_start",
                [
                    str(repo_root / "tests" / "httpbin" / "start_httpbin.sh"),
                    "--write-env",
                    str(httpbin_env_file),
                ],
                httpbin_start_log,
                cwd=repo_root,
            )
            results.append(start_res)
            if start_res.returncode == 0 and httpbin_env_file.exists():
                httpbin_started = True
                try:
                    httpbin_env = _parse_shell_exports(httpbin_env_file)
                except Exception as exc:
                    ok = False
                    _write_text(httpbin_dir / "httpbin_env_parse_error.txt", str(exc) + "\n")
                    add_policy_violation("env_preflight_httpbin_env_parse_error")
                container_name = httpbin_env.get("QCURL_HTTPBIN_CONTAINER_NAME", "")
            else:
                ok = False
                if start_res.returncode != 0:
                    add_policy_violation("env_preflight_httpbin_start_failed")
                elif not httpbin_env_file.exists():
                    add_policy_violation("env_preflight_httpbin_env_missing")

            if httpbin_started and httpbin_env.get("QCURL_HTTPBIN_URL"):
                env_for_ctest = os.environ.copy()
                env_for_ctest.update(httpbin_env)

                results.append(
                    _run_gate(
                        "ctest_list_env",
                        ["ctest", "-N", "--no-tests=error", "-L", "env"],
                        meta_dir / "ctest_list_env.txt",
                        cwd=build_dir,
                        env=env_for_ctest,
                    )
                )
                results.append(
                    _run_gate(
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
                )
                if results[-1].returncode != 0:
                    ok = False
                    add_policy_violation("gate_env_failed")
            else:
                # “基本无问题”验收口径：env 证据集合必须真实执行。
                # 若 httpbin 未就绪或无法提供 QCURL_HTTPBIN_URL，则视为“缺失证据”，必须失败。
                ok = False
                if httpbin_started and (not httpbin_env.get("QCURL_HTTPBIN_URL")):
                    add_policy_violation("env_preflight_httpbin_url_missing")
                _write_text(
                    httpbin_dir / "httpbin_unavailable.txt",
                    "httpbin 未就绪（docker/镜像/端口/健康检查失败或 env 缺失），因此无法运行 LABELS=env 证据门禁。\n",
                )
        finally:
            # best-effort stop（即使 start 失败也尝试清理默认容器名）
            stop_cmd = [str(repo_root / "tests" / "httpbin" / "stop_httpbin.sh")]
            if container_name:
                stop_cmd += ["--name", container_name]
            results.append(
                _run_gate(
                    "httpbin_stop",
                    stop_cmd,
                    httpbin_stop_log,
                    cwd=repo_root,
                )
            )

        # Gate 3: libcurl_consistency p0 gate
        _safe_mkdir(lc_reports_dir)
        results.append(
            _run_gate(
                "libcurl_consistency_gate_p0",
                [
                    "python3",
                    str(repo_root / "tests" / "libcurl_consistency" / "run_gate.py"),
                    "--suite",
                    "p0",
                    "--build",
                    "--qcurl-build",
                    str(build_dir),
                    "--reports-dir",
                    str(lc_reports_dir),
                ],
                logs_dir / "libcurl_consistency_gate_p0.log",
                cwd=repo_root,
            )
        )
        if results[-1].returncode != 0:
            ok = False
            add_policy_violation("gate_libcurl_consistency_p0_failed")
    except Exception as exc:
        ok = False
        manifest["exception"] = str(exc)
        add_policy_violation("gate_exception")
        _write_json(manifest_path, manifest)

    # Collect additional artifacts (best-effort)
    artifacts: Dict[str, object] = {}
    artifacts["meta_versions_txt"] = str(versions_txt)
    artifacts["httpbin_env_file"] = str(httpbin_env_file) if httpbin_env_file.exists() else ""
    artifacts["libcurl_consistency_reports_dir"] = str(lc_reports_dir) if lc_reports_dir.exists() else ""

    # Copy build/test-artifacts (if any) into evidence dir (avoid relying on CI upload of build/ tree)
    test_artifacts_src = build_dir / "test-artifacts"
    test_artifacts_dst = evidence_dir / "test-artifacts"
    artifacts["copied_test_artifacts"] = _best_effort_copytree(test_artifacts_src, test_artifacts_dst)

    # Copy curl testenv service logs (if any)
    curl_gen_artifacts = repo_root / "curl" / "tests" / "http" / "gen" / "artifacts"
    service_logs_dst = lc_dir / "service_logs"
    n_logs = _best_effort_copy_glob(curl_gen_artifacts, "**/service_logs/**", service_logs_dst)
    artifacts["copied_service_logs_files"] = int(n_logs)

    # Finalize manifest + tarball
    manifest["result"] = "pass" if ok else "fail"
    manifest["results"] = [
        {
            "name": r.name,
            "command": r.command,
            "returncode": r.returncode,
            "duration_s": r.duration_s,
            "log_file": r.log_file,
        }
        for r in results
    ]
    manifest["artifacts"] = artifacts
    manifest["policy_violations"] = list(policy_violations)
    _write_json(manifest_path, manifest)

    try:
        _tar_gz_dir(evidence_dir, tar_path)
    except Exception as exc:
        ok = False
        add_policy_violation("packaging_tar_gz_failed")
        sys.stderr.write(f"[basic_no_problem_gate] failed to create tar.gz: {exc}\n")
        # 写回 manifest，标记打包失败
        manifest["result"] = "fail"
        manifest["packaging"] = {"tar_gz_error": str(exc)}  # type: ignore[index]
        manifest["policy_violations"] = list(policy_violations)
        _write_json(manifest_path, manifest)

    sys.stderr.write(f"[basic_no_problem_gate] result={manifest.get('result')} evidence_dir={evidence_dir}\n")
    sys.stderr.write(f"[basic_no_problem_gate] tar_gz={tar_path}\n")
    return 0 if ok else 3


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
