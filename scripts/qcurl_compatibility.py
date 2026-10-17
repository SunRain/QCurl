#!/usr/bin/env python3
"""Capture references, check unchanged binaries, or explicitly rebuild old sources."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

if __package__:
    from .qcurl_compatibility_check import check_reference, rebuild_source
    from .qcurl_compatibility_common import CompatibilityError
    from .qcurl_compatibility_reference import (
        capture_reference,
        reference_file_entries,
        validate_reference,
        verify_capture,
    )
    from .qcurl_abi_common import AbiGateError
else:
    from qcurl_compatibility_check import check_reference, rebuild_source
    from qcurl_compatibility_common import CompatibilityError
    from qcurl_compatibility_reference import (
        capture_reference,
        reference_file_entries,
        validate_reference,
        verify_capture,
    )
    from qcurl_abi_common import AbiGateError

__all__ = [
    "capture_reference",
    "check_reference",
    "rebuild_source",
    "validate_reference",
    "reference_file_entries",
    "verify_capture",
]


def main(argv: list[str] | None = None) -> int:
    """Run the explicitly selected compatibility operation without mode fallbacks."""
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest="operation", required=True)
    for name in ("capture", "check", "rebuild-source"):
        command = subcommands.add_parser(name)
        command.add_argument(
            "--build-dir",
            type=Path,
            required=True,
            help="Producer with recorded compiler/dependency profile; check never rebuilds it",
        )
        command.add_argument("--stage-prefix", type=Path, required=True)
        command.add_argument("--output-dir", type=Path, required=True)
        if name != "capture":
            command.add_argument("--reference", type=Path, required=True)
        if name != "capture":
            command.add_argument(
                "--require-history",
                action="store_true",
                help="Reject self round-trips when a published patch reference is required",
            )
    args = parser.parse_args(argv)
    try:
        if args.operation == "capture":
            capture_reference(args.build_dir, args.stage_prefix, args.output_dir)
        elif args.operation == "check":
            check_reference(
                args.reference,
                args.stage_prefix,
                args.output_dir,
                build_dir=args.build_dir,
                require_history=args.require_history,
            )
        else:
            rebuild_source(
                args.reference,
                args.stage_prefix,
                args.output_dir,
                build_dir=args.build_dir,
                require_history=args.require_history,
            )
    except (CompatibilityError, AbiGateError, OSError, ValueError) as exc:
        print(f"compatibility failed: {exc}", file=sys.stderr)
        return 1
    print(f"compatibility evidence: {args.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
