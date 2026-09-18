"""Consumer smoke fixture contract checks for the public API gate."""

from __future__ import annotations

from argparse import Namespace
from pathlib import Path
from typing import Callable
import subprocess

from tests.public_api.consumer_build import build_consumer
from tests.public_api.consumer_build import configure_and_build
from tests.public_api.consumer_build import configure_consumer
from tests.public_api.consumer_build import run_consumer

from tests.public_api.consumer_contract_validators import validate_cache_core_contract_fixture
from tests.public_api.consumer_contract_validators import validate_cache_policy_core_contract_fixture
from tests.public_api.consumer_contract_validators import validate_cancel_token_core_contract_fixture
from tests.public_api.consumer_contract_validators import validate_connection_pool_core_contract_fixture
from tests.public_api.consumer_contract_validators import validate_default_logger_core_contract_fixture
from tests.public_api.consumer_contract_validators import validate_logger_core_contract_fixture
from tests.public_api.consumer_contract_validators import validate_middleware_core_contract_fixture
from tests.public_api.consumer_contract_validators import validate_multipart_core_contract_fixture
from tests.public_api.consumer_contract_validators import validate_request_config_core_contract_fixture
from tests.public_api.consumer_contract_validators import validate_reply_lifecycle_core_contract_fixture
from tests.public_api.consumer_contract_validators import validate_scheduler_core_contract_fixture
from tests.public_api.consumer_cookie_contracts import validate_cookie_async_result_core_contract_fixture
from tests.public_api.metatype_inventory import validate_metatype_inventory


RunCommand = Callable[..., subprocess.CompletedProcess[str]]
FailFunc = Callable[[str], int]


def _fixture_source(source_dir: Path) -> str:
    sources = sorted(source_dir.glob("*.cpp"))
    if not sources:
        raise RuntimeError(f"missing consumer fixture source: {source_dir}")
    return "\n".join(source.read_text(encoding="utf-8") for source in sources)


CONSUMER_FIXTURE_VALIDATORS = (
    validate_scheduler_core_contract_fixture,
    validate_logger_core_contract_fixture,
    validate_cache_policy_core_contract_fixture,
    validate_request_config_core_contract_fixture,
    validate_cache_core_contract_fixture,
    validate_multipart_core_contract_fixture,
    validate_default_logger_core_contract_fixture,
    validate_cancel_token_core_contract_fixture,
    validate_reply_lifecycle_core_contract_fixture,
    validate_connection_pool_core_contract_fixture,
    validate_cookie_async_result_core_contract_fixture,
    validate_middleware_core_contract_fixture,
)


def validate_consumer_fixture(source_dir: Path) -> None:
    """Run all positive consumer fixture contract validators."""

    for validator in CONSUMER_FIXTURE_VALIDATORS:
        validator(source_dir)


def validate_metatype_fixture(source_dir: Path) -> None:
    """Ensure the metatype consumer fixture keeps the static initialization contract."""

    source = _fixture_source(source_dir)
    required_snippets = [
        "#include <QCGlobal.h>",
        "#include <QCNetworkAccessManager.h>",
        "#include <QCNetworkLaneCancelResult.h>",
        "#include <QCNetworkLaneKey.h>",
        "#include <QCNetworkRequestPriority.h>",
        "#include <QCNetworkSchedulerPolicy.h>",
        "QCurl::initialize();",
        "QMetaType::fromType<QCurl::QCNetworkRequestPriority>()",
        "QMetaType::fromName(name)",
        "QMetaType::fromType<QCurl::QCNetworkLaneKey>()",
        "QMetaType::fromType<QCurl::QCNetworkSchedulerPolicy>()",
        "QMetaType::fromType<QCurl::QCNetworkSchedulerPolicy::LaneConfig>()",
        "QMetaType::fromType<QCurl::QCNetworkSchedulerStatistics>()",
        "QMetaType::fromType<QCurl::QCNetworkLaneCancelResult>()",
    ]
    missing = [snippet for snippet in required_snippets if snippet not in source]
    if missing:
        raise RuntimeError(
            "consumer metatype smoke fixture is missing required static initialization coverage: "
            + ", ".join(missing)
        )

    typed_queued_snippets = [
        "&MetatypeProbe::priorityReady",
        "Qt::QueuedConnection",
        "Q_EMIT probe.priorityReady(QCurl::QCNetworkRequestPriority::High)",
        "eventLoop.exec();",
    ]
    missing = [snippet for snippet in typed_queued_snippets if snippet not in source]
    if missing:
        raise RuntimeError(
            "consumer metatype smoke fixture is missing typed queued coverage: "
            + ", ".join(missing)
        )

    initialize_index = source.find("QCurl::initialize();")
    first_connection_index = source.find("QObject::connect(")
    if initialize_index < 0 or first_connection_index < 0 or initialize_index > first_connection_index:
        raise RuntimeError(
            "consumer metatype smoke fixture must call QCurl::initialize() "
            "before the first Qt connection"
        )

    public_api_root = Path(__file__).resolve().parent
    inventory_errors = validate_metatype_inventory(
        public_api_root / "metatype_inventory.json",
        public_api_root.parents[1] / "src",
        source_dir,
    )
    if inventory_errors:
        raise RuntimeError("metatype inventory validation failed: " + "; ".join(inventory_errors))


def run_metatype_consumer_smoke(args: Namespace, *, run_command: RunCommand, fail_func: FailFunc) -> int:
    """Verify enum-only metatype consumer builds and runs against the staged package."""

    try:
        validate_metatype_fixture(args.source_dir)
        configure_and_build(
            args.source_dir,
            args.build_dir,
            args.stage_dir,
            args.cmake,
            args.config,
            run_command,
            args.consumer_cache,
        )
        run_consumer(
            args.build_dir, "qcurl_public_api_consumer_metatype_smoke", args.config, run_command
        )
    except RuntimeError as exc:
        return fail_func(f"consumer metatype smoke failed: {exc}")

    print("[public_api] consumer metatype smoke passed")
    return 0


def _configure_negative_consumer(args: Namespace, run_command: RunCommand, fail_func: FailFunc) -> int | None:
    try:
        configure_consumer(
            args.negative_source_dir, args.negative_build_dir, args.stage_dir,
            args.cmake, args.consumer_cache, run_command,
        )
    except RuntimeError as exc:
        return fail_func(f"negative consumer configure failed unexpectedly: {exc}")
    return None


def _build_negative_consumer(args: Namespace, run_command: RunCommand, fail_func: FailFunc) -> int | None:
    proc = build_consumer(
        args.negative_build_dir, args.cmake, args.config, run_command, expect_success=False
    )
    if proc.returncode == 0:
        return fail_func("negative consumer unexpectedly built successfully")
    return None


def run_consumer_smoke(args: Namespace, *, run_command: RunCommand, fail_func: FailFunc) -> int:
    """Verify positive and negative staged consumer builds."""

    try:
        validate_consumer_fixture(args.positive_source_dir)
    except RuntimeError as exc:
        return fail_func(f"consumer smoke fixture check failed: {exc}")

    try:
        configure_and_build(
            args.positive_source_dir,
            args.positive_build_dir,
            args.stage_dir,
            args.cmake,
            args.config,
            run_command,
            args.consumer_cache,
        )
        run_consumer(
            args.positive_build_dir, "qcurl_public_api_consumer_smoke", args.config, run_command
        )
    except RuntimeError as exc:
        return fail_func(f"positive consumer smoke failed: {exc}")

    configure_error = _configure_negative_consumer(args, run_command, fail_func)
    if configure_error is not None:
        return configure_error

    build_error = _build_negative_consumer(args, run_command, fail_func)
    if build_error is not None:
        return build_error

    print("[public_api] consumer smoke passed")
    return 0
