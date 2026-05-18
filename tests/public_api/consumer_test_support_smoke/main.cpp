#include <QCNetworkAccessManager.h>
#include <QCNetworkMockHandler.h>
#include <QCNetworkRequest.h>
#include <QCNetworkTestSupport.h>

#include <QCoreApplication>
#include <QUrl>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QCurl::QCNetworkAccessManager manager;
    QCurl::QCNetworkRequest request(QUrl(QStringLiteral("https://example.invalid")));
    QCurl::QCNetworkMockHandler mockHandler;
    mockHandler.setCaptureEnabled(true);
    mockHandler.setCaptureBodyPreviewLimit(5);

    QCurl::QCNetworkCapturedRequest capturedRequest;
    capturedRequest.setUrl(request.url());
    capturedRequest.setMethod(QCurl::HttpMethod::Post);
    capturedRequest.addHeader(QByteArrayLiteral("X-Test"), QByteArrayLiteral("mock"));
    capturedRequest.setBodySize(7);
    capturedRequest.setBodyPreview(QByteArrayLiteral("payload").left(5));
    mockHandler.recordRequest(capturedRequest);

    const auto capturedRequests = mockHandler.takeCapturedRequests();
    if (capturedRequests.size() != 1 || capturedRequests.first().url() != request.url()
        || capturedRequests.first().method() != QCurl::HttpMethod::Post
        || capturedRequests.first().headers().size() != 1
        || capturedRequests.first().bodySize() != 7
        || capturedRequests.first().bodyPreview() != QByteArrayLiteral("paylo")) {
        return 1;
    }

    mockHandler.mockResponse(QCurl::HttpMethod::Get, request.url(), QByteArrayLiteral("mock-body"), 201);
    int mockStatus = 0;
    if (!mockHandler.hasMock(QCurl::HttpMethod::Get, request.url())
        || mockHandler.getMockResponse(QCurl::HttpMethod::Get, request.url(), mockStatus)
               != QByteArrayLiteral("mock-body")
        || mockStatus != 201) {
        return 2;
    }

    QCurl::TestSupport::setMockHandler(&manager, &mockHandler);
    if (QCurl::TestSupport::mockHandler(&manager) != &mockHandler) {
        return 3;
    }
    QCurl::TestSupport::setMockHandler(&manager, nullptr);
    if (QCurl::TestSupport::mockHandler(&manager) != nullptr) {
        return 4;
    }

    return 0;
}
