"""Blocking Extras install/export consumer contracts for the public API gate."""

from __future__ import annotations

from argparse import Namespace

from tests.public_api.component_contracts import check_opt_in_headers
from tests.public_api.component_contracts import run_opt_in_consumer_smoke
from tests.public_api.component_contracts import FailFunc
from tests.public_api.component_contracts import RunCommand


def validate_blocking_extras_fixture(source_dir) -> None:
    """Ensure Blocking Extras fixture covers bounded body and device-download contracts."""

    source = (source_dir / "main.cpp").read_text(encoding="utf-8")
    required = [
        "#include <QCBlockingNetworkClient.h>",
        "QCurl::QCBlockingRequestOptions requestOptions",
        "requestOptions.setMaxInMemoryBodyBytes(4096)",
        "requestOptions.setProgressCallback(recordProgress, &probe)",
        "requestOptions.progressCallbackUserData()",
        "client.send(request, QCurl::HttpMethod::Get, {}, requestOptions)",
        "client.downloadToDevice(request, &output, requestOptions)",
        "success.rawHeaders()",
        "success.rawHeaderList()",
        "success.bytesReceived()",
        "success.setDiagnosticCurlCode(7)",
    ]
    missing = [snippet for snippet in required if snippet not in source]
    if missing:
        raise RuntimeError(
            "blocking extras consumer fixture is missing required contract coverage: "
            + ", ".join(missing)
        )


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

    try:
        validate_blocking_extras_fixture(args.positive_source_dir)
    except RuntimeError as exc:
        return fail_func(f"blocking extras consumer fixture check failed: {exc}")

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
