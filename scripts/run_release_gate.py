#!/usr/bin/env python3
"""Run no-git QCurl release readiness gates."""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

if __package__:
    from . import release_identity
    from .release_gate_steps import GateStep
    from .release_gate_steps import build_steps as _build_steps
    from .release_gate_execution import artifact_path as _artifact_path
    from .release_gate_execution import execute_gate as _execute_gate_impl
    from .release_gate_execution import identity_build_dirs as _identity_build_dirs_impl
    from .release_gate_execution import manifest_authority_paths as _manifest_authority_paths
    from .release_gate_execution import required_artifacts as _required_artifacts
    from .release_gate_execution import verify_manifest as _verify_manifest_impl
    from .release_gate_execution import write_gate_manifest as _write_gate_manifest
    from .release_gate_execution import write_snapshot_only as _write_snapshot_only_impl
    from .release_gate_execution import source_identity_stable as _source_identity_stable
    from .release_gate_paths import resolve_paths as _resolve_paths
    from .release_gate_parser import add_abi_arguments as _add_abi_arguments
    from .release_gate_parser import add_manifest_arguments as _add_manifest_arguments
    from .release_metadata_gate import scan_metadata as _scan_metadata
    from .release_tree_model import compiler_family
    from .release_tree_model import required_tree_ids
    from .release_tree_model import sanitizer_matches
    from .release_tree_model import tree_path
    from .release_tree_model import tree_registry
else:
    import release_identity
    from release_gate_steps import GateStep
    from release_gate_steps import build_steps as _build_steps
    from release_gate_execution import artifact_path as _artifact_path
    from release_gate_execution import execute_gate as _execute_gate_impl
    from release_gate_execution import identity_build_dirs as _identity_build_dirs_impl
    from release_gate_execution import manifest_authority_paths as _manifest_authority_paths
    from release_gate_execution import required_artifacts as _required_artifacts
    from release_gate_execution import verify_manifest as _verify_manifest_impl
    from release_gate_execution import write_gate_manifest as _write_gate_manifest
    from release_gate_execution import write_snapshot_only as _write_snapshot_only_impl
    from release_gate_execution import source_identity_stable as _source_identity_stable
    from release_gate_paths import resolve_paths as _resolve_paths
    from release_gate_parser import add_abi_arguments as _add_abi_arguments
    from release_gate_parser import add_manifest_arguments as _add_manifest_arguments
    from release_metadata_gate import scan_metadata as _scan_metadata
    from release_tree_model import compiler_family
    from release_tree_model import required_tree_ids
    from release_tree_model import sanitizer_matches
    from release_tree_model import tree_path
    from release_tree_model import tree_registry


def _repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def _selected_steps(args: argparse.Namespace) -> list[GateStep]:
    order = {"fast": 0, "strict": 1, "full": 2}
    max_order = order[args.tier]
    return [step for step in _build_steps(args) if order[step.tier] <= max_order]


def _identity_build_dirs(args: argparse.Namespace) -> list[Path]:
    return [tree_path(args, tree_id) for tree_id in tree_registry(args)]


def _tree_registry(args: argparse.Namespace) -> dict[str, dict[str, object]]:
    """返回当前参数对应的六树 registry。"""

    return tree_registry(args)


def _build_cache_options(build_dir: Path) -> dict[str, str]:
    cache_path = build_dir / "CMakeCache.txt"
    if not cache_path.is_file():
        raise ValueError(
            "BUILD_TESTING capability cache is missing: " + str(cache_path)
        )

    options: dict[str, str] = {}
    for line in cache_path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = re.match(
            r"^(BUILD_TESTING|QCURL_BUILD_SHARED_LIBS|CMAKE_CXX_COMPILER|"
            r"QCURL_SANITIZER_PROFILE|CMAKE_CXX_FLAGS):[^=]*=(.*)$",
            line,
        )
        if match is not None:
            options[match.group(1)] = match.group(2)
    return options


def _validate_build_capabilities(args: argparse.Namespace) -> None:
    """要求所有必需 producer tree 满足固定能力矩阵。"""

    registry = _tree_registry(args)
    required = required_tree_ids(args.tier)
    missing = [tree_id for tree_id in required if tree_id not in registry]
    if missing:
        raise ValueError("release gate requires explicit tree path: " + ", ".join(missing))
    for tree_id in required:
        spec = registry[tree_id]
        build_dir = Path(spec["path"])
        options = _build_cache_options(build_dir)
        expected_testing = str(spec["build_testing"])
        expected_shared = str(spec["shared_libs"])
        expected_compiler = str(spec["compiler_family"])
        actual_testing = options.get("BUILD_TESTING")
        if actual_testing != expected_testing:
            actual = actual_testing if actual_testing is not None else "missing"
            raise ValueError(
                f"{tree_id} BUILD_TESTING must be {expected_testing}, got {actual}"
            )
        actual_shared = options.get("QCURL_BUILD_SHARED_LIBS")
        if actual_shared != expected_shared:
            actual = actual_shared if actual_shared is not None else "missing"
            raise ValueError(
                f"{tree_id} QCURL_BUILD_SHARED_LIBS must be {expected_shared}, got {actual}"
            )
        actual_compiler = compiler_family(options.get("CMAKE_CXX_COMPILER"))
        if actual_compiler != expected_compiler:
            raise ValueError(
                f"{tree_id} compiler family must be {expected_compiler}, got {actual_compiler}"
            )
        if not sanitizer_matches(options, spec.get("sanitizer_profile")):
            raise ValueError(f"{tree_id} sanitizer capability does not match its fixed profile")


def _format_command(command: list[str]) -> str:
    return " ".join(shlex.quote(part) for part in command)


def _run_step(step: GateStep, repo_root: Path) -> int:
    print(f"[release_gate] RUN {step.name}: {_format_command(step.command)}")
    env = os.environ.copy()
    if step.name == "libcurl_consistency_full":
        env.setdefault("QCURL_LC_EXT", "1")
    proc = subprocess.run(step.command, cwd=repo_root, env=env)
    if proc.returncode != 0:
        print(f"[release_gate] FAIL {step.name}: rc={proc.returncode}", file=sys.stderr)
    else:
        print(f"[release_gate] PASS {step.name}")
    return int(proc.returncode)


_verify_manifest = _verify_manifest_impl
_write_snapshot_only = _write_snapshot_only_impl


def _execute_gate(
    args: argparse.Namespace,
    repo_root: Path,
    steps: list[GateStep],
    authority_paths: list[Path],
) -> int:
    return _execute_gate_impl(
        args,
        repo_root,
        steps,
        authority_paths,
        run_step=_run_step,
    )


def _write_plan(args: argparse.Namespace, steps: list[GateStep]) -> None:
    payload = {
        "tier": args.tier,
        "trees": {
            tree_id: {
                **record,
                "path": str(record["path"]),
            }
            for tree_id, record in _tree_registry(args).items()
        },
        "steps": [
            {
                "name": step.name,
                "tier": step.tier,
                "description": step.description,
                "command": step.command,
                "producerTreeId": step.producer_tree_id,
                "requiredArtifactIds": list(step.required_artifact_ids),
            }
            for step in steps
        ],
    }
    print(json.dumps(payload, ensure_ascii=False, indent=2))


def _authority_paths(args: argparse.Namespace, repo_root: Path) -> list[Path]:
    paths = list(args.authority)
    if args.contract_json is not None:
        paths.append(args.contract_json)
    resolved = [
        (path if path.is_absolute() else repo_root / path).resolve()
        for path in paths
    ]
    if args.tier == "full":
        expected = release_identity.default_authority_paths(repo_root)
        if (
            len(resolved) != len(expected)
            or len(set(resolved)) != len(resolved)
            or set(resolved) != set(expected)
        ):
            raise ValueError(
                "full release gate requires the exact four authority inputs"
            )
        missing = [path for path in expected if not path.is_file()]
        if missing:
            raise ValueError(
                "full release gate authority input is not a regular file: "
                + ", ".join(str(path) for path in missing)
            )
    return sorted(set(resolved))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "QCurl no-git release gate. fast is not a Stable release gate; "
            "full includes ABI and capability evidence."
        )
    )
    parser.add_argument("--tier", choices=("fast", "strict", "full"), default="fast")
    parser.add_argument("--release-shared-build-dir", type=Path)
    parser.add_argument("--release-static-build-dir", type=Path)
    parser.add_argument("--test-shared-gcc-build-dir", type=Path)
    parser.add_argument("--test-shared-clang-build-dir", type=Path)
    parser.add_argument("--asan-ubsan-lsan-build-dir", type=Path)
    parser.add_argument("--tsan-build-dir", type=Path)
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--python", default=sys.executable)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--ctest", default="ctest")
    _add_manifest_arguments(parser)
    _add_abi_arguments(parser)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    repo_root = _repo_root()
    try:
        _resolve_paths(args, repo_root)
    except ValueError as exc:
        print(f"[release_gate] ERROR: {exc}", file=sys.stderr)
        return 2
    if args.scan_metadata:
        return _scan_metadata(repo_root)
    if args.tier == "full":
        try:
            _validate_build_capabilities(args)
        except ValueError as exc:
            print(f"[release_gate] ERROR: {exc}", file=sys.stderr)
            return 2
    steps = _selected_steps(args)
    if args.dry_run:
        _write_plan(args, steps)
        return 0
    if args.verify_manifest:
        return _verify_manifest(args, repo_root, steps)
    authority_paths = _authority_paths(args, repo_root)
    if args.snapshot_only:
        return _write_snapshot_only(args, repo_root, steps, authority_paths)
    return _execute_gate(args, repo_root, steps, authority_paths)


if __name__ == "__main__":
    raise SystemExit(main())
