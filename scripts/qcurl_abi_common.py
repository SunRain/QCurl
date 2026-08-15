"""QCurl ABI gate 的通用工具、路径和错误类型。"""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path


class AbiGateError(RuntimeError):
    """ABI gate 无法生成 release-grade 证据时抛出的错误。"""


def tool_path(name: str) -> str:
    """返回必需工具的绝对路径，缺失时失败。"""

    path = shutil.which(name)
    if not path:
        raise AbiGateError(f"required ABI tool not found: {name}")
    return path


def resolve_existing_file(path: Path, description: str) -> Path:
    """解析并要求输入为现存 regular file。"""

    resolved = path.resolve()
    if not resolved.is_file():
        raise AbiGateError(f"{description} not found: {resolved}")
    return resolved


def resolve_existing_dir(path: Path, description: str) -> Path:
    """解析并要求输入为现存目录。"""

    resolved = path.resolve()
    if not resolved.is_dir():
        raise AbiGateError(f"{description} not found: {resolved}")
    return resolved


def run(
    command: list[str],
    *,
    output_file: Path | None = None,
) -> subprocess.CompletedProcess[str]:
    """运行 ABI producer 命令，并对非零返回码 fail closed。"""

    proc = subprocess.run(command, text=True, capture_output=True)
    if output_file is not None:
        output_file.parent.mkdir(parents=True, exist_ok=True)
        output_file.write_text(
            (proc.stdout or "") + (proc.stderr or ""),
            encoding="utf-8",
        )
    if proc.returncode != 0:
        details = (proc.stdout or "") + (proc.stderr or "")
        raise AbiGateError(
            "command failed: "
            + " ".join(command)
            + f"\nreturncode={proc.returncode}\n"
            + details
        )
    return proc


def path_is_controlled_baseline(
    path: Path,
    *,
    repo_root: Path | None = None,
) -> bool:
    """判断路径是否位于受控 ABI baseline 目录。"""

    root = (repo_root or Path.cwd()).resolve()
    candidate = path.resolve() if path.is_absolute() else (root / path).resolve()
    controlled = (root / "abi" / "baseline").resolve()
    return candidate == controlled or controlled in candidate.parents


def validate_snapshot_output(
    path: Path,
    *,
    repo_root: Path | None = None,
) -> None:
    """阻止诊断 producer 修改受控 ABI baseline。"""

    if path_is_controlled_baseline(path, repo_root=repo_root):
        raise AbiGateError(
            "diagnostic baseline/snapshot commands cannot write abi/baseline; "
            "use the explicit promote command"
        )
