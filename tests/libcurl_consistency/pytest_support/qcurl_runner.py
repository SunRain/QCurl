"""
QCurl Qt Test 运行器（LC-4 占位）：
- 调用 Qt Test 可执行（后续由 CMake 产出），并写出 QCurl artifacts。
"""

from __future__ import annotations

import os
import subprocess
from pathlib import Path
from typing import Dict, List, Optional

from testenv import Env  # type: ignore

from .artifacts import (
    artifact_path,
    artifacts_root,
    build_request_semantic,
    build_response_summary,
    ensure_case_dir,
    write_json,
)

def _collect_download_files(run_dir: Path, count: int) -> List[Path]:
    return [run_dir / f"download_{i}.data" for i in range(count)]

def run_qt_test(
    env: Env,
    suite: str,
    case: str,
    qt_executable: Path,
    args: Optional[List[str]] = None,
    request_meta: Optional[Dict] = None,
    response_meta: Optional[Dict] = None,
    download_files: Optional[List[Path]] = None,
    download_count: Optional[int] = None,
    case_env: Optional[Dict[str, str]] = None,
) -> Dict:
    """
    执行 Qt Test 可执行，生成 QCurl artifacts。
    - qt_executable：Qt Test 产物路径
    - args：传给 Qt Test 的参数（如 gtest filter/自定义输出路径）
    - request_meta/response_meta：Qt Test 侧填充 method/url/headers/http_version/status 等
    - download_files：下载场景传入文件路径以计算 len/hash
    """
    if not qt_executable.exists():
        raise FileNotFoundError(f"Qt Test binary not found: {qt_executable}")
    cmd = [str(qt_executable)]
    if args:
        cmd += args

    root = artifacts_root(env)
    case_dir = ensure_case_dir(root, suite=suite, case=case)
    run_dir = case_dir / "qcurl_run"
    run_dir.mkdir(parents=True, exist_ok=True)

    run_env = os.environ.copy()
    if case_env:
        run_env.update(case_env)
    run_env.setdefault("QCURL_LC_OUT_DIR", str(run_dir))

    proc = subprocess.run(cmd, cwd=str(run_dir), env=run_env, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"Qt Test failed ({proc.returncode}): {cmd}\n{proc.stdout}\n{proc.stderr}")

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
            files = _collect_download_files(run_dir, download_count)
        body = response_meta.get("body")
        if files:
            # 对于 QCurl 侧，优先基于实际落盘结果计算 hash/len
            body = None
        resp_summary = build_response_summary(
            status=response_meta["status"],
            http_version=response_meta["http_version"],
            headers=response_meta.get("headers"),
            body=body,
            body_files=files,
        )

    payload: Dict[str, object] = {
        "runner": "qcurl",
        "binary": str(qt_executable),
        "args": args or [],
        "request": req_semantic,
        "response": resp_summary,
        "stdout": proc.stdout.splitlines(),
        "stderr": proc.stderr.splitlines(),
    }
    path = artifact_path(root, suite=suite, case=case, flavor="qcurl")
    write_json(path, payload)
    return {"path": path, "payload": payload}
