"""Test Support install/export consumer contracts for the public API gate."""

from __future__ import annotations

from argparse import Namespace

from tests.public_api.component_contracts import check_opt_in_headers
from tests.public_api.component_contracts import run_opt_in_consumer_smoke
from tests.public_api.component_contracts import FailFunc
from tests.public_api.component_contracts import RunCommand
from tests.public_api.consumer_contract_validators import validate_mock_handler_test_support_fixture


def check_test_support_install(args: Namespace, *, fail_func: FailFunc) -> int:
    """Verify Test Support headers are opt-in and absent from the default Core stage."""

    return check_opt_in_headers(
        manifest=args.manifest,
        default_stage_dir=args.default_stage_dir,
        component_stage_dir=args.test_support_stage_dir,
        component_label="test support",
        fail_func=fail_func,
    )


def run_test_support_consumer_smoke(
    args: Namespace,
    *,
    run_command: RunCommand,
    fail_func: FailFunc,
) -> int:
    """Verify opt-in Test Support consumer builds and default Core negative builds."""

    try:
        validate_mock_handler_test_support_fixture(args.positive_source_dir)
    except RuntimeError as exc:
        return fail_func(f"test support consumer fixture check failed: {exc}")

    return run_opt_in_consumer_smoke(
        cmake=args.cmake,
        component_stage_dir=args.test_support_stage_dir,
        default_stage_dir=args.default_stage_dir,
        positive_source_dir=args.positive_source_dir,
        positive_build_dir=args.positive_build_dir,
        negative_source_dir=args.negative_source_dir,
        negative_build_dir=args.negative_build_dir,
        config=args.config,
        component_label="test support",
        run_command=run_command,
        fail_func=fail_func,
    )
