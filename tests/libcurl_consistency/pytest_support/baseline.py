"""
libcurl baseline 运行器（LC-3）：
- 默认复用 LocalClient(name='cli_*') 执行 libtests。
- 少量用例使用 repo 内置 baseline 可执行（与 QCURL_QTTEST 同目录），避免修改 curl/ 目录。
- 生成 baseline artifacts（JSON）并落盘。
"""

from __future__ import annotations

import os
import subprocess
import time
from pathlib import Path
from typing import Dict, List, Optional, Set

from testenv import Env, LocalClient  # type: ignore

from .artifacts import (
    artifact_path,
    artifacts_root,
    build_request_semantic,
    build_response_summary,
    write_json,
)


def _collect_download_files(client: LocalClient, count: int) -> list[Path]:
    """按 LocalClient 约定命名收集 download_* 文件。"""
    return [Path(client.download_file(i)) for i in range(count)]

def _default_range_resume_baseline_binary() -> Path:
    qt_bin = os.environ.get("QCURL_QTTEST", "").strip()
    if qt_bin:
        return Path(qt_bin).resolve().with_name("qcurl_lc_range_resume_baseline")
    return Path("qcurl_lc_range_resume_baseline")


def _default_postfields_binary_baseline_binary() -> Path:
    qt_bin = os.environ.get("QCURL_QTTEST", "").strip()
    if qt_bin:
        return Path(qt_bin).resolve().with_name("qcurl_lc_postfields_binary_baseline")
    return Path("qcurl_lc_postfields_binary_baseline")

def _default_multi_get4_baseline_binary() -> Path:
    qt_bin = os.environ.get("QCURL_QTTEST", "").strip()
    if qt_bin:
        return Path(qt_bin).resolve().with_name("qcurl_lc_multi_get4_baseline")
    return Path("qcurl_lc_multi_get4_baseline")

def _default_http_baseline_binary() -> Path:
    qt_bin = os.environ.get("QCURL_QTTEST", "").strip()
    if qt_bin:
        return Path(qt_bin).resolve().with_name("qcurl_lc_http_baseline")
    return Path("qcurl_lc_http_baseline")

def _default_pause_resume_baseline_binary() -> Path:
    qt_bin = os.environ.get("QCURL_QTTEST", "").strip()
    if qt_bin:
        return Path(qt_bin).resolve().with_name("qcurl_lc_pause_resume_baseline")
    return Path("qcurl_lc_pause_resume_baseline")


def _run_standalone_baseline(env: Env,
                             *,
                             executable: Path,
                             args: List[str],
                             cwd: Path,
                             allowed_exit_codes: Set[int]) -> tuple[List[str], List[str], int, int]:
    started = time.monotonic()
    proc = subprocess.run(
        [str(executable), *args],
        cwd=cwd,
        capture_output=True,
        text=True,
        timeout=float(getattr(env, "test_timeout", 60.0)),
        check=False,
    )
    duration_ms = int((time.monotonic() - started) * 1000)
    stdout_lines = proc.stdout.splitlines(keepends=True) if proc.stdout else []
    stderr_lines = proc.stderr.splitlines(keepends=True) if proc.stderr else []
    if proc.returncode not in allowed_exit_codes:
        raise AssertionError(
            f"baseline failed ({proc.returncode}): {executable} {args}\n"
            f"{proc.stdout}\n{proc.stderr}"
        )
    return stdout_lines, stderr_lines, duration_ms, int(proc.returncode)

def run_libtest_case(
    env: Env,
    suite: str,
    case: str,
    client_name: str,
    args: Optional[List[str]] = None,
    request_meta: Optional[Dict] = None,
    response_meta: Optional[Dict] = None,
    download_files: Optional[List[Path]] = None,
    download_count: Optional[int] = None,
    allowed_exit_codes: Optional[Set[int]] = None,
) -> Dict:
    """
    运行 libcurl baseline 用例并返回 artifacts 结构。
    - suite/case：用于 artifacts 路径组织
    - client_name：对应 `LocalClient(name=...)`
    - args：传给 libtests 的参数
    - request_meta/response_meta：由调用方填充 method/url/headers/http_version/status 等
    - download_files：下载场景传入文件路径以计算 len/hash
    - download_count：如未传 download_files，可按 LocalClient 规则自动收集 download_{i}.data
    """
    client = LocalClient(env=env, name=client_name)
    cmd_args = args or []

    baseline_executable = None
    stdout_lines: List[str] = []
    stderr_lines: List[str] = []
    duration_ms = 0
    exit_code = 0
    allowed = allowed_exit_codes or {0}

    if client_name == "cli_hx_range_resume":
        baseline_executable = (
            Path(os.environ.get("QCURL_LC_RANGE_RESUME_BASELINE", "")).resolve()
            if os.environ.get("QCURL_LC_RANGE_RESUME_BASELINE")
            else _default_range_resume_baseline_binary()
        )
        if not baseline_executable.exists():
            raise FileNotFoundError(f"range resume baseline 可执行不存在：{baseline_executable}")
        stdout_lines, stderr_lines, duration_ms, exit_code = _run_standalone_baseline(
            env,
            executable=baseline_executable,
            args=cmd_args,
            cwd=client.run_dir,
            allowed_exit_codes=allowed,
        )
    elif client_name == "cli_postfields_binary":
        baseline_executable = (
            Path(os.environ.get("QCURL_LC_POSTFIELDS_BINARY_BASELINE", "")).resolve()
            if os.environ.get("QCURL_LC_POSTFIELDS_BINARY_BASELINE")
            else _default_postfields_binary_baseline_binary()
        )
        if not baseline_executable.exists():
            raise FileNotFoundError(f"postfields binary baseline 可执行不存在：{baseline_executable}")
        stdout_lines, stderr_lines, duration_ms, exit_code = _run_standalone_baseline(
            env,
            executable=baseline_executable,
            args=cmd_args,
            cwd=client.run_dir,
            allowed_exit_codes=allowed,
        )
    elif client_name == "cli_hx_multi_get4":
        baseline_executable = (
            Path(os.environ.get("QCURL_LC_MULTI_GET4_BASELINE", "")).resolve()
            if os.environ.get("QCURL_LC_MULTI_GET4_BASELINE")
            else _default_multi_get4_baseline_binary()
        )
        if not baseline_executable.exists():
            raise FileNotFoundError(f"multi-get4 baseline 可执行不存在：{baseline_executable}")
        stdout_lines, stderr_lines, duration_ms, exit_code = _run_standalone_baseline(
            env,
            executable=baseline_executable,
            args=cmd_args,
            cwd=client.run_dir,
            allowed_exit_codes=allowed,
        )
    elif client_name == "cli_lc_http":
        baseline_executable = (
            Path(os.environ.get("QCURL_LC_HTTP_BASELINE", "")).resolve()
            if os.environ.get("QCURL_LC_HTTP_BASELINE")
            else _default_http_baseline_binary()
        )
        if not baseline_executable.exists():
            raise FileNotFoundError(f"http baseline 可执行不存在：{baseline_executable}")
        stdout_lines, stderr_lines, duration_ms, exit_code = _run_standalone_baseline(
            env,
            executable=baseline_executable,
            args=cmd_args,
            cwd=client.run_dir,
            allowed_exit_codes=allowed,
        )
    elif client_name == "cli_lc_pause_resume":
        baseline_executable = (
            Path(os.environ.get("QCURL_LC_PAUSE_RESUME_BASELINE", "")).resolve()
            if os.environ.get("QCURL_LC_PAUSE_RESUME_BASELINE")
            else _default_pause_resume_baseline_binary()
        )
        if not baseline_executable.exists():
            raise FileNotFoundError(f"pause/resume baseline 可执行不存在：{baseline_executable}")
        stdout_lines, stderr_lines, duration_ms, exit_code = _run_standalone_baseline(
            env,
            executable=baseline_executable,
            args=cmd_args,
            cwd=client.run_dir,
            allowed_exit_codes=allowed,
        )
    else:
        if not client.exists():
            raise FileNotFoundError(f"LocalClient not built: {client_name}")
        result = client.run(args=cmd_args)
        if result.exit_code not in allowed:
            raise AssertionError(f"LocalClient failed ({result.exit_code}): {client_name} {cmd_args}")
        # testenv.ExecResult 的 stdout/stderr 属性是拼接后的字符串；为保持 artifacts 一致性，这里统一写为“行列表”。
        stdout_lines = result.stdout.splitlines(keepends=True) if result.stdout else []
        try:
            stderr_lines = list(result.trace_lines)
        except Exception:
            stderr_lines = result.stderr.splitlines(keepends=True) if result.stderr else []
        duration_ms = int(result.duration.total_seconds() * 1000)
        exit_code = int(result.exit_code)

    req_semantic = None
    if request_meta:
        req_semantic = build_request_semantic(
            method=request_meta["method"],
            url=request_meta["url"],
            headers=request_meta.get("headers"),
            body=request_meta.get("body"),
        )
    resp_summary = None
    if response_meta:
        files = download_files
        if files is None and download_count:
            files = _collect_download_files(client, download_count)
        resp_summary = build_response_summary(
            status=response_meta["status"],
            http_version=response_meta["http_version"],
            headers=response_meta.get("headers"),
            body=response_meta.get("body"),
            body_files=files,
        )

    payload: Dict[str, object] = {
        "runner": "libcurl",
        "client": client_name,
        "args": cmd_args,
        "baseline_executable": str(baseline_executable) if baseline_executable else None,
        "exit_code": exit_code,
        "request": req_semantic,
        "response": resp_summary,
        "stdout": stdout_lines,
        "stderr": stderr_lines,
        "duration_ms": duration_ms,
    }
    root = artifacts_root(env)
    path = artifact_path(root, suite=suite, case=case, flavor="baseline")
    write_json(path, payload)
    return {"path": path, "payload": payload}
