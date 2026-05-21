"""Other Extras fixture checks for the public API gate."""

from __future__ import annotations

from pathlib import Path

from tests.public_api.consumer_contract_validators import _fixture_source
from tests.public_api.consumer_contract_validators import _require_snippets


def validate_other_extras_fixture(source_dir: Path) -> None:
    """Ensure Other Extras consumer smoke keeps opt-in diagnostics coverage."""

    _require_snippets(
        _fixture_source(source_dir),
        [
            "#include <QCNetworkDiagnostics.h>",
            "#include <QCNetworkMiddlewareExtras.h>",
            "QCurl::DiagResult result",
            "result.success = true",
            "result.summary = QStringLiteral(\"other-extras\")",
            "result.details.insert(",
            "result.toString()",
            "QCurl::QCRedactingLoggingMiddleware redactingLog",
            "QCurl::QCObservabilityMiddleware observability",
        ],
        "other extras consumer fixture is missing Diagnostics or Middleware Extras opt-in coverage",
    )
