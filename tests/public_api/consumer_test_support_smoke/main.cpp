#include <QCNetworkAccessManager.h>
#include <QCNetworkMockHandler.h>
#include <QCNetworkReply.h>
#include <QCNetworkRequest.h>
#include <QCNetworkRetryPolicy.h>
#include <QCNetworkTestSupport.h>
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>

static bool retryBudgetContract(QCurl::QCNetworkAccessManager &manager,
                                QCurl::QCNetworkMockHandler &handler)
{
    QCurl::QCNetworkRequest request(QUrl(QStringLiteral("https://example.invalid/budget")));
    QCurl::QCNetworkRetryPolicy policy;
    using Result = QCurl::QCNetworkRetryPolicy::UpdateResult;
    if (policy.setMaxRetries(1) != Result::Applied
        || policy.setMaxCumulativeDelay(std::chrono::milliseconds(0)) != Result::Applied
        || policy.setMaxElapsedTime(std::chrono::seconds(2)) != Result::Applied
        || policy.maxCumulativeDelay() != std::chrono::milliseconds(0)
        || policy.maxElapsedTime() != std::chrono::seconds(2)) {
        return false;
    }
    request.setRetryPolicy(policy);
    handler.mockResponse(QCurl::HttpMethod::Get,
                         request.url(),
                         QByteArrayLiteral("limited"),
                         429,
                         {{QByteArrayLiteral("Retry-After"), QByteArrayLiteral("7")}});
    QCurl::TestSupport::setMockHandler(&manager, &handler);
    auto *reply = manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QCurl::QCNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();
    const bool passed = reply->isFinished()
                        && reply->error() == QCurl::NetworkError::HttpTooManyRequests
                        && reply->retryAttemptCount() == 0
                        && reply->cumulativeRetryDelay() == std::chrono::milliseconds(0)
                        && reply->retryAfterDelay() == std::chrono::seconds(7);
    QCurl::TestSupport::setMockHandler(&manager, nullptr);
    return passed;
}

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

    mockHandler.mockResponse(QCurl::HttpMethod::Get,
                             request.url(),
                             QByteArrayLiteral("mock-body"),
                             201);
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

    return retryBudgetContract(manager, mockHandler) ? 0 : 5;
}
