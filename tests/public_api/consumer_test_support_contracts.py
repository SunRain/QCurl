"""Test Support fixture checks for the public API gate."""

from __future__ import annotations

from pathlib import Path

from tests.public_api.consumer_contract_validators import _fixture_source
from tests.public_api.consumer_contract_validators import _find_present_snippets
from tests.public_api.consumer_contract_validators import _require_snippets


def validate_mock_handler_test_support_fixture(source_dir: Path) -> None:
    """Ensure Test Support consumer smoke keeps MockHandler coverage."""

    source = _fixture_source(source_dir)
    _require_snippets(
        source,
        [
            "#include <QCNetworkMockHandler.h>",
            "#include <QCNetworkTestSupport.h>",
            "QCurl::QCNetworkMockHandler mockHandler",
            "QCurl::QCNetworkCapturedRequest capturedRequest",
            "capturedRequest.setUrl(request.url())",
            "capturedRequest.setMethod(QCurl::HttpMethod::Post)",
            "capturedRequest.addHeader(",
            "capturedRequest.setBodySize(",
            "capturedRequest.setBodyPreview(",
            "mockHandler.recordRequest(capturedRequest)",
            "mockHandler.takeCapturedRequests()",
            "capturedRequests.first().url()",
            "capturedRequests.first().method()",
            "capturedRequests.first().headers()",
            "capturedRequests.first().bodySize()",
            "capturedRequests.first().bodyPreview()",
            "mockHandler.mockResponse(",
            "mockHandler.hasMock(",
            "mockHandler.getMockResponse(",
            "QCurl::TestSupport::setMockHandler(&manager, &mockHandler)",
            "QCurl::TestSupport::mockHandler(&manager)",
        ],
        "consumer smoke fixture is missing required MockHandler contract coverage",
    )
    present = _find_present_snippets(
        source,
        [
            ".url =",
            ".method =",
            ".headers =",
            ".bodyPreview =",
            ".bodySize =",
            ".followLocation =",
            ".connectTimeoutMs =",
            ".totalTimeoutMs =",
        ],
    )
    if present:
        raise RuntimeError(
            "consumer smoke fixture must use CapturedRequest accessor API only; found: "
            + ", ".join(present)
        )
