#!/usr/bin/env python3
"""Run the minimal UCE gate and archive its evidence bundle."""

from __future__ import annotations

import argparse
import os
import sys
from datetime import datetime, timezone
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from scripts.uce_gate.orchestrator import run_uce_gate


def main(argv: list[str] | None = None) -> int:
    """CLI entry point."""

    parser = argparse.ArgumentParser(description="Run the minimal UCE gate and archive evidence.")
    parser.add_argument(
        "--tier",
        choices=("pr", "nightly", "soak"),
        default="pr",
        help="UCE tier to execute (default: pr).",
    )
    parser.add_argument(
        "--build-dir",
        default=os.environ.get("QCURL_BUILD_DIR", "build"),
        help="CMake build directory (default: build/ or $QCURL_BUILD_DIR).",
    )
    parser.add_argument(
        "--run-id",
        default=os.environ.get("QCURL_UCE_RUN_ID", ""),
        help="Run identifier used in evidence dir name.",
    )
    parser.add_argument(
        "--evidence-root",
        default=os.environ.get("QCURL_UCE_EVIDENCE_ROOT", ""),
        help="Evidence root directory (default: <build-dir>/evidence/uce).",
    )
    args = parser.parse_args(argv)

    repo_root = Path(__file__).resolve().parent.parent
    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = (repo_root / build_dir).resolve()
    if not build_dir.exists():
        sys.stderr.write(f"[run_uce_gate] build dir not found: {build_dir}\n")
        return 2

    run_id = (args.run_id or "").strip() or datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    return run_uce_gate(
        repo_root=repo_root,
        build_dir=build_dir,
        tier=args.tier,
        run_id=run_id,
        evidence_root_arg=args.evidence_root,
    )


if __name__ == "__main__":
    raise SystemExit(main())
