"""Cookie async result fixture checks for the public API gate."""

from __future__ import annotations

from pathlib import Path

from tests.public_api.consumer_contract_validators import _fixture_source
from tests.public_api.consumer_contract_validators import _require_snippets


def validate_cookie_async_result_core_contract_fixture(source_dir: Path) -> None:
    """Ensure consumer smoke keeps cookie async result Core coverage."""

    _require_snippets(
        _fixture_source(source_dir),
        [
            "#include <QCCookieAsyncResult.h>",
            "#include <QNetworkCookie>",
            "QCurl::QCCookieOperationResult::success()",
            "QCurl::QCCookieOperationResult::failure(",
            "cookieImportSuccess.isSuccess()",
            "cookieImportFailure.error()",
            "QCurl::QCCookieExportResult::success(",
            "QCurl::QCCookieExportResult::failure(",
            "cookieExportSuccess.cookies()",
            "cookieExportFailure.error()",
        ],
        "consumer smoke fixture is missing required cookie async result contract coverage",
    )
