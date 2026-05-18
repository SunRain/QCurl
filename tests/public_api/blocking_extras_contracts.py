"""Blocking Extras install/export consumer contracts for the public API gate."""

from __future__ import annotations

from argparse import Namespace

from tests.public_api.component_contracts import check_opt_in_headers
from tests.public_api.component_contracts import run_opt_in_consumer_smoke
from tests.public_api.component_contracts import FailFunc
from tests.public_api.component_contracts import RunCommand


def check_blocking_extras_install(args: Namespace, *, fail_func: FailFunc) -> int:
    """Verify Blocking Extras headers are opt-in and absent from the default Core stage."""

    return check_opt_in_headers(
        manifest=args.manifest,
        default_stage_dir=args.default_stage_dir,
        component_stage_dir=args.blocking_stage_dir,
        component_label="blocking extras",
        fail_func=fail_func,
    )


def run_blocking_extras_consumer_smoke(
    args: Namespace,
    *,
    run_command: RunCommand,
    fail_func: FailFunc,
) -> int:
    """Verify opt-in Blocking Extras consumer builds and default Core negative builds."""

    return run_opt_in_consumer_smoke(
        cmake=args.cmake,
        component_stage_dir=args.blocking_stage_dir,
        default_stage_dir=args.default_stage_dir,
        positive_source_dir=args.positive_source_dir,
        positive_build_dir=args.positive_build_dir,
        negative_source_dir=args.negative_source_dir,
        negative_build_dir=args.negative_build_dir,
        config=args.config,
        component_label="blocking extras",
        run_command=run_command,
        fail_func=fail_func,
    )
