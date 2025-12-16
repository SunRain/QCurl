"""
libcurl baseline 运行器（LC-3）：
- 复用 LocalClient(name='cli_*') 执行 libtests。
- 生成 baseline artifacts（JSON）并落盘。
"""

from __future__ import annotations

from pathlib import Path
from typing import Dict, List, Optional

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
    if not client.exists():
        raise FileNotFoundError(f"LocalClient not built: {client_name}")
    cmd_args = args or []
    result = client.run(args=cmd_args)
    result.check_exit_code(0)

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
        "request": req_semantic,
        "response": resp_summary,
        "stdout": result.stdout,
        "stderr": result.stderr,
        "duration_ms": int(result.duration.total_seconds() * 1000),
    }
    root = artifacts_root(env)
    path = artifact_path(root, suite=suite, case=case, flavor="baseline")
    write_json(path, payload)
    return {"path": path, "payload": payload}
