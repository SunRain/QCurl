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
            "QCurl::QCNetworkDiagnosticsOptions diagnosticsOptions",
            "QFuture<QCurl::DiagResult>",
            "QCurl::QCNetworkDiagnostics::resolveDNS(",
            "diagnosticsOptions.setTimeout(",
            "diagnosticsOptions.setPort(",
            "result.setSuccess(true)",
            "result.setSummary(QStringLiteral(\"other-extras\"))",
            "result.setDetail(",
            "result.details().contains(",
            "result.toString()",
            "QCurl::QCRedactingLoggingMiddleware redactingLog",
            "QCurl::QCObservabilityMiddleware observability",
            "#include <QCWebSocket.h>",
            "QCurl::QCWebSocketOptions socketOptions",
            "socketOptions.setConnectTimeout(",
            "socketOptions.setMaxFrameBytes(",
            "socketOptions.setMaxMessageBytes(",
            "socketOptions.setMaxPendingSendBytes(",
            "socketOptions.setMaxReceiveBufferBytes(",
            "socketOptions.setCloseHandshakeTimeout(",
            "QCurl::QCWebSocket socket",
            "#include <QCWebSocketPool.h>",
            "QCurl::QCWebSocketPoolConfig poolConfig",
            "poolConfig.setMaxPoolSize(4)",
            "QCurl::QCWebSocketPool pool(poolConfig)",
            "QFuture<QCurl::QCWebSocketAcquireResult>",
            "QFuture<QCurl::QCWebSocketPreWarmResult>",
            "QCWebSocketAcquireResult::Status::WrongThread",
            "wrongThreadAcquire.leaseId()",
            "QCurl::QCWebSocketPool::LeaseId",
            "QCurl::QCWebSocketPool::LeaseResult",
            "pool.resolveLease(",
            "pool.release(0)",
            "QCurl::QCWebSocketPoolStats poolStats",
            "poolStats.totalConnections()",
        ],
        "other extras consumer fixture is missing Diagnostics, Middleware Extras or WebSocket Preview opt-in coverage",
    )
