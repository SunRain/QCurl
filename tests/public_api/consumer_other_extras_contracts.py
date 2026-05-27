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
            "result.setSuccess(true)",
            "result.setSummary(QStringLiteral(\"other-extras\"))",
            "result.setDetail(",
            "result.details().contains(",
            "result.toString()",
            "QCurl::QCRedactingLoggingMiddleware redactingLog",
            "QCurl::QCObservabilityMiddleware observability",
            "#include <QCWebSocket.h>",
            "#include <QCWebSocketCompressionConfig.h>",
            "QCurl::QCWebSocket socket",
            "QCurl::QCWebSocketCompressionConfig::defaultConfig()",
        ],
        "other extras consumer fixture is missing Diagnostics, Middleware Extras or WebSocket Preview opt-in coverage",
    )
