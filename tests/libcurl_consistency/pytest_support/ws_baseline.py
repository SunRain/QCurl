"""
WS baseline 运行器（LC-19/LC-20）：
- 运行自建的 libcurl WS baseline 可执行（`qcurl_lc_ws_baseline`）
- 输出 download_0.data（事件序列），并生成 baseline artifacts（JSON）
"""

from __future__ import annotations

import subprocess
from pathlib import Path
from typing import Dict, Optional

from testenv import Env  # type: ignore

from .artifacts import (
    artifact_path,
    artifacts_root,
    build_request_semantic,
    build_response_summary,
    ensure_case_dir,
    write_json,
)


def run_ws_baseline_case(
    env: Env,
    *,
    suite: str,
    case: str,
    baseline_executable: Path,
    scenario: str,
    url: str,
    timeout_ms: int = 20000,
    request_meta: Optional[Dict] = None,
    response_meta: Optional[Dict] = None,
) -> Dict:
    if not baseline_executable.exists():
        raise FileNotFoundError(f"WS baseline binary not found: {baseline_executable}")

    root = artifacts_root(env)
    case_dir = ensure_case_dir(root, suite=suite, case=case)
    run_dir = case_dir / "baseline_run"
    run_dir.mkdir(parents=True, exist_ok=True)
    out_file = run_dir / "download_0.data"

    cmd = [
        str(baseline_executable),
        scenario,
        url,
        str(out_file),
        str(timeout_ms),
    ]
    proc = subprocess.run(
        cmd,
        cwd=str(run_dir),
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(f"WS baseline failed ({proc.returncode}): {cmd}\n{proc.stdout}\n{proc.stderr}")

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
        resp_summary = build_response_summary(
            status=response_meta["status"],
            http_version=response_meta["http_version"],
            headers=response_meta.get("headers"),
            body=None,
            body_files=[out_file],
        )

    payload: Dict[str, object] = {
        "runner": "libcurl",
        "binary": str(baseline_executable),
        "scenario": scenario,
        "url": url,
        "request": req_semantic,
        "response": resp_summary,
        "stdout": proc.stdout.splitlines(),
        "stderr": proc.stderr.splitlines(),
    }
    path = artifact_path(root, suite=suite, case=case, flavor="baseline")
    write_json(path, payload)
    return {"path": path, "payload": payload}

