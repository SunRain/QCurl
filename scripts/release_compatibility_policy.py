"""从候选版本和正式 tag 重算版本线兼容输入。"""

from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path
from typing import Any

if __package__:
    from .check_release_contract import parse_project_version
    from .release_gate_model import GateTier
    from . import release_identity
else:
    from check_release_contract import parse_project_version
    from release_gate_model import GateTier
    import release_identity


def candidate_version(args: argparse.Namespace) -> tuple[int, int, int]:
    """读取参数所指源码树的唯一发布版本。"""

    return parse_project_version(Path(args.source_root))


def version_text(version: tuple[int, int, int]) -> str:
    """将发布版本元组写为完整版本号。"""

    return ".".join(map(str, version))


def validate_abi_arguments(args: argparse.Namespace, repo: Path) -> None:
    """拒绝首版/patch 的模式绕过和不完整历史输入。"""

    mode = args.abi_mode
    references = (args.abi_reference_first, args.abi_reference_latest)
    if mode != "current" and any(path is not None for path in references):
        raise ValueError(f"{mode} ABI mode rejects historical reference arguments")
    if mode == "current" and any(path is None for path in references):
        raise ValueError(
            "current ABI mode requires --abi-reference-first and --abi-reference-latest"
        )
    if args.stage == "final":
        if args.tier is not GateTier.FULL:
            raise ValueError("final qualification requires --tier full")
        if mode == "none":
            raise ValueError(
                "final qualification requires snapshot or current ABI mode"
            )
    if mode == "none":
        return
    version = parse_project_version(repo)
    if version[2] == 0 and mode != "snapshot":
        raise ValueError("a version-line first release requires snapshot ABI mode")
    if version[2] > 0 and mode != "current":
        raise ValueError("a patch release requires current ABI mode")


def _git(repo: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", *args], cwd=repo, text=True, capture_output=True, check=False
    )
    if result.returncode:
        raise ValueError(f"cannot verify release tag: {result.stderr.strip()}")
    return result.stdout.strip()


def published_reference_versions(
    repo: Path, version: tuple[int, int, int]
) -> list[tuple[int, int, int]]:
    """取得已在本地核对的同线前序正式 tag，不访问远端。"""

    prefix = f"v{version[0]}.{version[1]}."
    result = []
    for tag in _git(repo, "tag", "--list", prefix + "*").splitlines():
        match = re.fullmatch(r"v(\d+)\.(\d+)\.(\d+)", tag)
        if match:
            value = tuple(map(int, match.groups()))
            if value[:2] == version[:2] and value < version:
                result.append(value)
    result.sort()
    if not result or result[0] != (*version[:2], 0):
        raise ValueError(
            "published first reference tag is missing for this version line"
        )
    return result


def require_clean_identity(identity: dict[str, Any]) -> None:
    """要求用于 final 或历史参考的源码和递归子模块干净。"""

    for section in ("tracked_patch", "untracked"):
        entries = identity.get(section, {}).get("entries")
        if not isinstance(entries, list):
            raise ValueError(
                f"final/reference requires complete source identity: {section}"
            )
        if entries:
            raise ValueError("final/reference requires a clean candidate")
    submodules = identity.get("submodules", {}).get("entries")
    if not isinstance(submodules, list) or any(
        not isinstance(entry, dict) or entry.get("dirty") is not False
        for entry in submodules
    ):
        raise ValueError("final/reference requires clean recursive submodules")
    if not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", str(identity.get("head", ""))):
        raise ValueError("candidate commit identity is missing")


def historical_references(
    args: argparse.Namespace, repo: Path
) -> list[tuple[str, Path, dict[str, Any]]]:
    """验证首版/最近 patch 的资产、版本及 tag/commit 后去重。"""

    if args.abi_mode != "current":
        return []
    if __package__:
        from .qcurl_compatibility import validate_reference
    else:
        from qcurl_compatibility import validate_reference
    version = candidate_version(args)
    published = published_reference_versions(repo, version)
    expected = {"first": published[0], "latest": published[-1]}
    head = _git(repo, "rev-parse", "HEAD")
    records = []
    for role in ("first", "latest"):
        path = Path(getattr(args, f"abi_reference_{role}"))
        payload = validate_reference(path)
        wanted = version_text(expected[role])
        line = f"{version[0]}.{version[1]}"
        if payload.get("version") != wanted or payload.get("line") != line:
            raise ValueError(
                f"{role} reference must be published version {wanted} in this line"
            )
        identity = payload.get("identity", {})
        require_clean_identity(identity)
        tag_commit = _git(repo, "rev-parse", f"refs/tags/v{wanted}^{{commit}}")
        if identity.get("head") != tag_commit or tag_commit == head:
            raise ValueError(
                f"{role} reference does not match its historical tag/commit"
            )
        _git(repo, "merge-base", "--is-ancestor", tag_commit, head)
        records.append((role, path, payload))
    if expected["first"] == expected["latest"]:
        if records[0][2] != records[1][2]:
            raise ValueError(
                "first/latest references for the same release have different contents"
            )
        return records[:1]
    return records


def reference_input_records(args: argparse.Namespace, repo: Path) -> dict[str, Any]:
    """绑定所有历史索引及其实际输入，复用现有文件摘要。"""

    records = {}
    for role, path, payload in historical_references(args, repo):
        entries = [
            {
                "path": "reference.json",
                "sha256": release_identity.file_sha256(path / "reference.json"),
            }
        ]
        entries.extend(payload["files"])
        records[role] = {"path": str(path), "files": entries}
    return records
