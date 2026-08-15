"""Machine-owned identity and result checks for release/UCE evidence.

The manifest is a snapshot, not a human-edited status report.  A result can be
published only after the same snapshot still describes the worktree, build
capabilities, authority inputs, and required gate results.
"""

from __future__ import annotations

import hashlib
import json
import platform
import re
import shutil
import subprocess
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence

SCHEMA = "qa-manifest@v1"
MANIFEST_PAYLOAD_DIGEST_FIELD = "manifestPayloadSha256"

AUTHORITY_RELATIVE_PATHS = (
    ".helloagents/plans/202608051224_qcurl_v2_comprehensive_review_remediation/requirements.md",
    ".helloagents/plans/202608051224_qcurl_v2_comprehensive_review_remediation/plan.md",
    ".helloagents/plans/202608051224_qcurl_v2_comprehensive_review_remediation/contract.json",
    "docs/arch/2.0.0-hard-break-release-contract.md",
)


def _run(
    repo: Path,
    args: Sequence[str],
    *,
    timeout: float = 10.0,
    preserve_nul: bool = False,
) -> str:
    try:
        completed = subprocess.run(
            list(args),
            cwd=repo,
            check=False,
            capture_output=True,
            text=True,
            errors="surrogateescape",
            timeout=timeout,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return f"<unavailable:{type(exc).__name__}>"
    output = completed.stdout or completed.stderr
    return output if preserve_nul else output.strip()


def _sha_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def file_sha256(path: Path) -> str:
    """Return a stable content digest for a regular artifact file."""
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _canonical(value: Any) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode()


def _digest(value: Any) -> str:
    return _sha_bytes(_canonical(value))


def _normalize_tree_registry(
    tree_registry: Mapping[str, Any] | None,
) -> dict[str, dict[str, Any]]:
    """将 tree registry 转为可稳定序列化的 identity 数据。"""

    if tree_registry is None:
        return {}
    normalized: dict[str, dict[str, Any]] = {}
    for tree_id, value in sorted(tree_registry.items()):
        if not isinstance(value, Mapping):
            continue
        record = dict(value)
        path = record.get("path")
        if isinstance(path, Path):
            record["path"] = str(path.resolve())
        normalized[str(tree_id)] = record
    return normalized


def manifest_payload_digest(manifest: dict[str, Any]) -> str:
    """Return the canonical digest excluding the payload digest field itself."""
    payload = dict(manifest)
    payload.pop(MANIFEST_PAYLOAD_DIGEST_FIELD, None)
    return _digest(payload)


def _file_entry(repo: Path, relative: str) -> dict[str, Any]:
    path = repo / relative
    try:
        stat = path.lstat()
    except FileNotFoundError:
        return {"path": relative, "type": "missing", "digest": None}
    if path.is_symlink():
        target = path.readlink().as_posix().encode()
        return {"path": relative, "type": "symlink", "digest": _sha_bytes(target)}
    if path.is_file():
        return {
            "path": relative,
            "type": "file",
            "digest": file_sha256(path),
            "size": stat.st_size,
        }
    return {"path": relative, "type": "other", "digest": None, "mode": stat.st_mode}


def _nul_lines(value: str) -> list[str]:
    return [item for item in value.split("\0") if item]


def _git_bytes(repo: Path, args: Sequence[str]) -> bytes:
    try:
        completed = subprocess.run(
            list(args), cwd=repo, check=False, capture_output=True, timeout=20.0
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return f"<unavailable:{type(exc).__name__}>".encode()
    return completed.stdout or completed.stderr


def _tracked_patch(repo: Path) -> dict[str, Any]:
    patch = _git_bytes(
        repo,
        ["git", "diff", "--binary", "--no-ext-diff", "--no-color", "HEAD", "--"],
    )
    changed = _nul_lines(
        _run(
            repo,
            ["git", "diff", "--name-only", "-z", "HEAD", "--"],
            preserve_nul=True,
        )
    )
    entries = [_file_entry(repo, item) for item in sorted(changed)]
    return {
        "patch_digest": _sha_bytes(patch),
        "content_digest": _digest(entries),
        "entries": entries,
    }


def _untracked(repo: Path) -> dict[str, Any]:
    paths = _nul_lines(
        _run(
            repo,
            ["git", "ls-files", "--others", "--exclude-standard", "-z"],
            preserve_nul=True,
        )
    )
    entries = [_file_entry(repo, item) for item in sorted(paths)]
    return {"digest": _digest(entries), "entries": entries}


def _submodules(repo: Path) -> dict[str, Any]:
    entries: list[dict[str, Any]] = []
    for line in _run(repo, ["git", "submodule", "status", "--recursive"]).splitlines():
        match = re.match(r"(?P<marker>[-+U ])(?P<sha>[0-9a-f]+) (?P<path>[^ ]+)", line)
        if not match:
            continue
        path = repo / match.group("path")
        checked_out = (
            _run(path, ["git", "rev-parse", "HEAD"]) if path.is_dir() else "missing"
        )
        dirty = (
            bool(_run(path, ["git", "status", "--porcelain", "--untracked-files=all"]))
            if path.is_dir()
            else True
        )
        entries.append(
            {
                "path": match.group("path"),
                "index_sha": match.group("sha"),
                "sha": checked_out,
                "dirty": dirty,
                "tracked_patch": _tracked_patch(path) if path.is_dir() else None,
                "untracked": _untracked(path) if path.is_dir() else None,
            }
        )
    entries.sort(key=lambda item: item["path"])
    return {"digest": _digest(entries), "entries": entries}


def _cache_options(path: Path) -> dict[str, Any]:
    if not path.is_file():
        return {"path": str(path), "exists": False, "options": {}}
    options: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = re.match(r"^([^:#][^:]*):[^=]*=(.*)$", line)
        if not match:
            continue
        key, value = match.groups()
        if key.startswith(("QCURL_", "CMAKE_", "BUILD_")) or key in {
            "CMAKE_BUILD_TYPE",
            "BUILD_SHARED_LIBS",
        }:
            options[key] = value
    return {"path": str(path), "exists": True, "options": dict(sorted(options.items()))}


def _toolchain(repo: Path, caches: Sequence[dict[str, Any]]) -> dict[str, Any]:
    compiler_paths = {
        cache["options"].get("CMAKE_CXX_COMPILER")
        for cache in caches
        if isinstance(cache.get("options"), dict)
    }
    compiler_paths.discard(None)
    compiler_paths.update(("c++", "g++", "clang++"))
    compilers = {
        str(Path(shutil.which(path) or path).resolve()) for path in compiler_paths
    }
    curl_header = repo / "curl" / "include" / "curl" / "curlver.h"
    curl_source_version = "<unavailable>"
    if curl_header.is_file():
        match = re.search(
            r'^#define LIBCURL_VERSION "([^"]+)"',
            curl_header.read_text(encoding="utf-8"),
            re.MULTILINE,
        )
        if match:
            curl_source_version = match.group(1)
    return {
        "cmake": _run(repo, ["cmake", "--version"]),
        "compilers": {
            path: _run(repo, [path, "--version"]) for path in sorted(compilers)
        },
        "libcurl": {
            "pkg_config": _run(repo, ["pkg-config", "--modversion", "libcurl"]),
            "vendored": curl_source_version,
        },
        "qt": {
            "pkg_config": _run(repo, ["pkg-config", "--modversion", "Qt6Core"]),
            "qmake": _run(repo, ["qmake6", "-query", "QT_VERSION"]),
        },
        "libabigail": {
            "abidiff": _run(repo, ["abidiff", "--version"]),
            "abidw": _run(repo, ["abidw", "--version"]),
        },
        "sanitizers": {
            name: _run(
                repo,
                [shutil.which("c++") or "c++", "-print-file-name=lib" + name + ".so"],
            )
            for name in ("asan", "ubsan", "tsan")
        },
    }


def _authority(repo: Path, paths: Iterable[Path]) -> dict[str, Any]:
    entries: list[dict[str, Any]] = []
    for candidate in paths:
        path = candidate if candidate.is_absolute() else repo / candidate
        relative = (
            path.relative_to(repo).as_posix()
            if path.is_relative_to(repo)
            else str(path)
        )
        entry = (
            _file_entry(repo, relative)
            if path.is_relative_to(repo)
            else {"path": relative, "type": "missing", "digest": None}
        )
        if path.is_file() and not path.is_relative_to(repo):
            entry = {
                "path": relative,
                "type": "file",
                "digest": file_sha256(path),
                "size": path.stat().st_size,
            }
        entries.append(entry)
    entries.sort(key=lambda item: item["path"])
    return {"digest": _digest(entries), "entries": entries}


def default_authority_paths(repo: Path) -> list[Path]:
    """Return the four immutable authority inputs for the v2 release."""
    return [(repo / relative).resolve() for relative in AUTHORITY_RELATIVE_PATHS]


def build_identity(
    repo: Path,
    *,
    build_dirs: Sequence[Path],
    authority_paths: Sequence[Path],
    commands: Sequence[Sequence[str]],
    tree_registry: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    caches = [_cache_options(path / "CMakeCache.txt") for path in build_dirs]
    normalized_trees = _normalize_tree_registry(tree_registry)
    if normalized_trees:
        cache_by_path = {cache["path"]: cache for cache in caches}
        for record in normalized_trees.values():
            cache = cache_by_path.get(str(record.get("path")))
            if cache is not None:
                record["cache"] = cache
    return {
        "head": _run(repo, ["git", "rev-parse", "HEAD"]),
        "tracked_patch": _tracked_patch(repo),
        "untracked": _untracked(repo),
        "submodules": _submodules(repo),
        "capabilities": {
            "build_dirs": [str(path) for path in build_dirs],
            "caches": caches,
            "tree_registry": normalized_trees,
            "digest": _digest(caches),
        },
        "toolchain": _toolchain(repo, caches),
        "platform": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "python": platform.python_version(),
        },
        "commands": [list(command) for command in commands],
        "authority": _authority(repo, authority_paths),
    }


if __package__:
    from .release_manifest import SnapshotCheck
    from .release_manifest import create_snapshot
    from .release_manifest import verify_snapshot
    from .release_manifest import write_snapshot
else:
    from release_manifest import SnapshotCheck
    from release_manifest import create_snapshot
    from release_manifest import verify_snapshot
    from release_manifest import write_snapshot
