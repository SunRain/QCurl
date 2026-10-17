"""release gate 的路径解析与阶段参数验证。"""

from __future__ import annotations

import argparse
from pathlib import Path

if __package__:
    from .release_compatibility_policy import validate_abi_arguments
    from .release_tree_model import required_tree_ids, tree_registry
else:
    from release_compatibility_policy import validate_abi_arguments
    from release_tree_model import required_tree_ids, tree_registry


_TREE_ARGUMENTS = (
    "release_shared_build_dir",
    "release_static_build_dir",
    "test_shared_gcc_build_dir",
    "test_shared_clang_build_dir",
    "asan_ubsan_lsan_build_dir",
)


def _resolve_tree_paths(args: argparse.Namespace, repo_root: Path) -> None:
    for argument in _TREE_ARGUMENTS:
        value = getattr(args, argument)
        if value is not None and not value.is_absolute():
            setattr(args, argument, (repo_root / value).resolve())


def _validate_required_trees(args: argparse.Namespace) -> None:
    if args.verify_manifest or args.scan_metadata:
        return
    registry = tree_registry(args)
    missing = [
        tree_id for tree_id in required_tree_ids(args.tier) if tree_id not in registry
    ]
    if missing:
        raise ValueError(
            "release gate requires explicit tree path: " + ", ".join(missing)
        )


def _resolve_common_paths(args: argparse.Namespace, repo_root: Path) -> None:
    if args.manifest is None:
        release_shared = getattr(args, "release_shared_build_dir", None)
        args.manifest = (
            release_shared / "release" / "qa-manifest.json"
            if release_shared is not None
            else repo_root / "artifacts" / "qa-manifest.json"
        )
    elif not args.manifest.is_absolute():
        args.manifest = (repo_root / args.manifest).resolve()
    args.authority = [
        path if path.is_absolute() else (repo_root / path).resolve()
        for path in args.authority
    ]
    args.required_artifact = [
        path if path.is_absolute() else (repo_root / path).resolve()
        for path in args.required_artifact
    ]


def resolve_paths(args: argparse.Namespace, repo_root: Path) -> None:
    """解析路径，并依据真实候选版本验证 ABI 参数。"""

    args.source_root = repo_root.resolve()
    _resolve_tree_paths(args, repo_root)
    _validate_required_trees(args)
    _resolve_common_paths(args, repo_root)
    for argument in ("abi_reference_first", "abi_reference_latest"):
        path = getattr(args, argument)
        if path is not None:
            setattr(
                args,
                argument,
                (path if path.is_absolute() else repo_root / path).resolve(),
            )
    if not args.scan_metadata:
        validate_abi_arguments(args, repo_root)
