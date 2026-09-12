"""采集 UCE 候选源码、index、子模块和权威文件的内容身份。"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
from typing import Any, Sequence

from scripts.release_identity import AUTHORITY_RELATIVE_PATHS


def _git_bytes(repo_root: Path, arguments: list[str]) -> bytes:
    completed = subprocess.run(
        ["git", "--no-optional-locks", *arguments],
        cwd=repo_root,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=30,
    )
    if completed.returncode != 0:
        detail = completed.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(f"候选身份 Git 查询失败 ({' '.join(arguments)}): {detail}")
    return completed.stdout


def _canonical(payload: Any) -> bytes:
    return json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")


def _byte_identity(payload: bytes) -> dict[str, Any]:
    return {"sha256": hashlib.sha256(payload).hexdigest(), "bytes": len(payload)}


def _file_record(repo_root: Path, relative: str) -> dict[str, Any]:
    path = repo_root / relative
    metadata = path.lstat()
    record = {"path": relative, "mode": format(stat.S_IMODE(metadata.st_mode), "04o")}
    if stat.S_ISLNK(metadata.st_mode):
        return record | {"type": "symlink"} | _byte_identity(os.fsencode(os.readlink(path)))
    if not stat.S_ISREG(metadata.st_mode):
        raise RuntimeError(f"候选包含无法采集内容的文件类型: {relative}")
    return record | {"type": "file"} | _content_identity(path)


def _content_identity(path: Path) -> dict[str, Any]:
    digest = hashlib.sha256()
    byte_count = 0
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
            byte_count += len(chunk)
    return {"bytes": byte_count, "sha256": digest.hexdigest()}


def _untracked_records(repo_root: Path, pathspecs: list[str]) -> dict[str, Any]:
    paths = _git_bytes(
        repo_root, ["ls-files", "--others", "--exclude-standard", "-z", *pathspecs]
    ).split(b"\0")
    records = [_file_record(repo_root, os.fsdecode(path)) for path in sorted(paths) if path]
    return _byte_identity(_canonical(records)) | {"files": records}


def _index_identity(repo_root: Path, pathspecs: list[str]) -> dict[str, Any]:
    payload = _git_bytes(repo_root, ["ls-files", "--stage", "-z", *pathspecs])
    entries = []
    for raw_entry in payload.split(b"\0"):
        if not raw_entry:
            continue
        metadata, path = raw_entry.split(b"\t", 1)
        mode, object_id, stage = metadata.decode("ascii").split()
        entries.append(
            {"path": os.fsdecode(path), "mode": mode, "object_id": object_id, "stage": int(stage)}
        )
    return _byte_identity(payload) | {"entries": entries}


def _authority_identity(repo_root: Path, relative_paths: Sequence[str]) -> dict[str, Any]:
    records = []
    for relative in sorted(relative_paths):
        path = repo_root / relative
        try:
            records.append({"path": relative} | _content_identity(path))
        except OSError as exc:
            raise RuntimeError(f"无法读取必需发布权威文件 {relative}: {exc}") from exc
    return _byte_identity(_canonical(records)) | {"files": records}


def _submodule_identity(
    repo_root: Path, index: dict[str, Any], ancestors: set[Path], excluded_paths: Sequence[Path]
) -> dict[str, Any]:
    paths = sorted({entry["path"] for entry in index["entries"] if entry["mode"] == "160000"})
    records = []
    for relative in paths:
        child = repo_root / relative
        try:
            actual_root = Path(os.fsdecode(_git_bytes(child, ["rev-parse", "--show-toplevel"]).strip()))
            if actual_root.resolve() != child.resolve():
                raise RuntimeError("gitlink 未初始化为独立仓库")
            child_identity = _tree_identity(child, ancestors, excluded_paths)
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
            raise RuntimeError(f"无法采集 gitlink 子模块 {relative}: {exc}") from exc
        records.append(
            {
                "path": relative,
                "gitlink_entries": [entry for entry in index["entries"] if entry["path"] == relative],
                "fingerprint": child_identity,
            }
        )
    return _byte_identity(_canonical(records)) | {"entries": records}


def _pathspecs(repo_root: Path, excluded_paths: Sequence[Path]) -> list[str]:
    exclusions = []
    for path in excluded_paths:
        try:
            relative = path.relative_to(repo_root)
        except ValueError:
            continue
        if relative == Path("."):
            raise ValueError("不能从候选身份中排除整个仓库")
        exclusions.append(f":(top,exclude,literal){relative.as_posix()}")
    return ["--", ".", *sorted(set(exclusions))]


def _tree_identity(
    repo_root: Path, ancestors: set[Path], excluded_paths: Sequence[Path]
) -> dict[str, Any]:
    resolved_root = repo_root.resolve()
    if resolved_root in ancestors:
        raise RuntimeError(f"子模块仓库形成循环: {resolved_root}")
    head = _git_bytes(repo_root, ["rev-parse", "HEAD"]).decode("ascii").strip()
    pathspecs = _pathspecs(repo_root, excluded_paths)
    # 子模块 dirty 标记不携带内容且会受取证输出影响；其完整内容由递归快照负责。
    diff_arguments = [
        "diff", "--binary", "--no-ext-diff", "--no-textconv", "--no-color", "--ignore-submodules=dirty"
    ]
    unstaged = _git_bytes(repo_root, [*diff_arguments, *pathspecs])
    staged = _git_bytes(repo_root, [*diff_arguments, "--cached", "HEAD", *pathspecs])
    index = _index_identity(repo_root, pathspecs)
    identity = {
        "head": head,
        "unstaged": _byte_identity(unstaged),
        "staged": _byte_identity(staged),
        "index": index,
        "untracked": _untracked_records(repo_root, pathspecs),
        "submodules": _submodule_identity(
            repo_root, index, ancestors | {resolved_root}, excluded_paths
        ),
    }
    return identity | {"combined": f"sha256:{_byte_identity(_canonical(identity))['sha256']}"}


def capture_candidate_fingerprint(
    repo_root: Path,
    *,
    authority_paths: Sequence[str] = AUTHORITY_RELATIVE_PATHS,
    excluded_paths: Sequence[Path] = (),
) -> dict[str, Any]:
    """采集完整候选身份；显式排除本轮输出，必需输入缺失时抛出错误。

    authority_paths 始终按实际文件内容读取，不受 Git 忽略规则或输出排除项影响。
    excluded_paths 接受绝对路径或相对 repo_root 的路径，不允许排除整个仓库。
    """

    repo_root = repo_root.resolve()
    exclusions = tuple(
        Path(os.path.abspath(path if path.is_absolute() else repo_root / path))
        for path in excluded_paths
    )
    identity = _tree_identity(repo_root, set(), exclusions)
    identity.pop("combined")
    identity["authority"] = _authority_identity(repo_root, authority_paths)
    return {
        "schemaVersion": 3,
        "repositoryRoot": str(repo_root.resolve()),
        **identity,
        "available": True,
        "combined": f"sha256:{_byte_identity(_canonical(identity))['sha256']}",
    }
