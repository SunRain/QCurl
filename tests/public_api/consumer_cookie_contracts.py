"""Cookie async result fixture checks for the public API gate."""

from __future__ import annotations

from pathlib import Path

from tests.public_api.consumer_contract_validators import _fixture_source
from tests.public_api.consumer_contract_validators import _require_snippets


def validate_cookie_async_result_core_contract_fixture(source_dir: Path) -> None:
    """Ensure consumer smoke keeps cookie async result Core coverage."""

    source = _fixture_source(source_dir)
    if "QNetworkCookie" in source:
        raise RuntimeError("consumer smoke fixture must not use QNetworkCookie")

    cmake_path = source_dir / "CMakeLists.txt"
    if cmake_path.exists():
        cmake_source = cmake_path.read_text(encoding="utf-8")
        forbidden_cmake = [
            "find_package(Qt6 REQUIRED COMPONENTS Core Network)",
            "Qt6::Network",
        ]
        found_forbidden = [snippet for snippet in forbidden_cmake if snippet in cmake_source]
        if found_forbidden:
            raise RuntimeError(
                "consumer smoke fixture must not require QtNetwork: "
                + ", ".join(found_forbidden)
            )

    _require_snippets(
        source,
        [
            "#include <QCCookie.h>",
            "#include <QCCookieAsyncResult.h>",
            "QCurl::QCCookieOperationResult::success()",
            "QCurl::QCCookieOperationResult::failure(",
            "cookieImportSuccess.isSuccess()",
            "cookieImportFailure.error()",
            "QCurl::QCCookie exportedCookie",
            "exportedCookie.setName(",
            "exportedCookie.setValue(",
            "QCurl::QCCookieExportResult::success(",
            "QCurl::QCCookieExportResult::failure(",
            "cookieExportSuccess.cookies()",
            "cookieExportFailure.error()",
        ],
        "consumer smoke fixture is missing required cookie async result contract coverage",
    )
