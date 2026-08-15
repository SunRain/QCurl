"""release gate 的路径解析与阶段参数验证。"""

from __future__ import annotations

import argparse
from pathlib import Path

if __package__:
    from .release_tree_model import required_tree_ids, tree_registry
else:
    from release_tree_model import required_tree_ids, tree_registry


_TREE_ARGUMENTS = (
    "release_shared_build_dir",
    "release_static_build_dir",
    "test_shared_gcc_build_dir",
    "test_shared_clang_build_dir",
    "asan_ubsan_lsan_build_dir",
    "tsan_build_dir",
)


def _validate_abi_mode(args: argparse.Namespace) -> None:
    hardbreak_parameters = (
        args.abi_hardbreak_baseline,
        args.abi_hardbreak_report,
        args.abi_hardbreak_current_snapshot,
    )
    if args.abi_mode == "current" and any(
        path is not None for path in hardbreak_parameters
    ):
        raise ValueError(
            "current ABI mode rejects promotion-candidate hard-break parameters"
        )
    if args.abi_mode == "promotion-candidate" and any(
        path is None for path in hardbreak_parameters
    ):
        raise ValueError(
            "promotion-candidate ABI mode requires --abi-hardbreak-baseline, "
            "--abi-hardbreak-report and --abi-hardbreak-current-snapshot"
        )
    if args.stage == "promotion" and args.abi_mode != "promotion-candidate":
        raise ValueError("promotion stage requires promotion-candidate ABI mode")
    if args.abi_mode == "promotion-candidate" and args.stage != "promotion":
        raise ValueError("promotion-candidate ABI mode requires promotion stage")


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
    if args.contract_json is not None and not args.contract_json.is_absolute():
        args.contract_json = (repo_root / args.contract_json).resolve()
    args.required_artifact = [
        path if path.is_absolute() else (repo_root / path).resolve()
        for path in args.required_artifact
    ]


def _resolve_abi_paths(args: argparse.Namespace, repo_root: Path) -> None:
    baseline = args.abi_hardbreak_baseline
    if baseline is not None and not baseline.is_absolute():
        args.abi_hardbreak_baseline = (repo_root / baseline).resolve()
    if args.abi_mode != "promotion-candidate":
        return
    if not args.abi_hardbreak_report.is_absolute():
        args.abi_hardbreak_report = (repo_root / args.abi_hardbreak_report).resolve()
    if not args.abi_hardbreak_current_snapshot.is_absolute():
        args.abi_hardbreak_current_snapshot = (
            repo_root / args.abi_hardbreak_current_snapshot
        ).resolve()


def _validate_promotion_outputs(args: argparse.Namespace) -> None:
    if args.abi_mode != "promotion-candidate":
        return
    release_shared = args.release_shared_build_dir
    if release_shared is None:
        raise ValueError("promotion-candidate ABI mode requires release-shared tree")
    expected_outputs = {
        "--abi-hardbreak-report": (
            release_shared / "abi" / "qcurl-core-v1-to-v2.abidiff.txt"
        ).resolve(),
        "--abi-hardbreak-current-snapshot": (
            release_shared
            / "abi"
            / "qcurl-core-v2.promotion-candidate.abi.xml"
        ).resolve(),
    }
    actual_outputs = {
        "--abi-hardbreak-report": args.abi_hardbreak_report,
        "--abi-hardbreak-current-snapshot": args.abi_hardbreak_current_snapshot,
    }
    drift = [
        name for name, expected in expected_outputs.items() if actual_outputs[name] != expected
    ]
    if drift:
        raise ValueError(
            "promotion-candidate requires fixed release-shared ABI output: "
            + ", ".join(drift)
        )


def resolve_paths(args: argparse.Namespace, repo_root: Path) -> None:
    """解析路径并在执行 release gate 前验证阶段参数。"""

    _validate_abi_mode(args)
    _resolve_tree_paths(args, repo_root)
    _validate_required_trees(args)
    _resolve_common_paths(args, repo_root)
    _resolve_abi_paths(args, repo_root)
    _validate_promotion_outputs(args)
