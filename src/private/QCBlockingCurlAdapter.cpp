#include "CurlFeatureProbe.h"
#include "QCNetworkError.h"
#include "private/CurlGlobalConstructor_p.h"
#include "private/QCBlockingCurlAdapter_p.h"
#include "private/QCBlockingCurlMethodSetup_p.h"
#include "private/QCBlockingCurlRequestSetup_p.h"
#include "private/QCCurlOptionAdapter_p.h"

#include <QIODevice>
#include <QScopeGuard>

#include <curl/curl.h>
#include <limits>
#include <utility>

namespace QCurl::Internal {
namespace {

struct ResponseSink
{
    QByteArray *body        = nullptr;
    QIODevice *device       = nullptr;
    qint64 maxInMemoryBytes = 0;
    qint64 bytesReceived    = 0;
    qint64 abortAfterBytes  = -1;
    QString failureMessage;
    bool cancelledByProgress = false;
};

struct BlockingProgressState
{
    QCBlockingProgressCallback callback = nullptr;
    void *userData                      = nullptr;
    QString failureMessage;
};

struct HeaderSink
{
    QCBlockingNetworkResult::HeaderList *headers = nullptr;
    QString failureMessage;
};

bool calculateCallbackBytes(size_t size, size_t nmemb, qint64 *bytes)
{
    if (size != 0 && nmemb > std::numeric_limits<size_t>::max() / size) {
        return false;
    }

    const size_t totalSize = size * nmemb;
    if (totalSize > static_cast<size_t>(std::numeric_limits<qint64>::max())) {
        return false;
    }

    *bytes = static_cast<qint64>(totalSize);
    return true;
}

bool canAppendToMemorySink(const ResponseSink &sink, qint64 totalSize)
{
    if (totalSize > std::numeric_limits<qsizetype>::max()) {
        return false;
    }
    if (sink.bytesReceived > std::numeric_limits<qint64>::max() - totalSize) {
        return false;
    }

    return sink.maxInMemoryBytes < 0 || totalSize <= sink.maxInMemoryBytes - sink.bytesReceived;
}

size_t writeBodyCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *sink = static_cast<ResponseSink *>(userdata);
    if (!sink) {
        return 0;
    }

    qint64 totalSize = 0;
    if (!calculateCallbackBytes(size, nmemb, &totalSize)) {
        sink->failureMessage = QStringLiteral("Blocking Extras response body chunk is too large");
        return 0;
    }
    if (totalSize <= 0) {
        return 0;
    }

    qint64 bytesToAccept = totalSize;
    if (sink->abortAfterBytes >= 0) {
        const qint64 remainingBytes = qMax<qint64>(0, sink->abortAfterBytes - sink->bytesReceived);
        bytesToAccept               = qMin(totalSize, remainingBytes);
    }

    if (sink->device && bytesToAccept > 0) {
        const qint64 written = sink->device->write(ptr, bytesToAccept);
        if (written != bytesToAccept) {
            sink->failureMessage = QStringLiteral("Blocking Extras output device write failed");
            return 0;
        }
    } else if (sink->body && bytesToAccept > 0) {
        if (!canAppendToMemorySink(*sink, bytesToAccept)) {
            sink->failureMessage = QStringLiteral(
                "Blocking Extras response body exceeds maxInMemoryBodyBytes");
            return 0;
        }
        sink->body->append(ptr, static_cast<qsizetype>(bytesToAccept));
    } else if (!sink->body && !sink->device) {
        return 0;
    }

    sink->bytesReceived += bytesToAccept;
    if (sink->abortAfterBytes >= 0 && sink->bytesReceived >= sink->abortAfterBytes) {
        sink->cancelledByProgress = true;
        return 0;
    }

    return static_cast<size_t>(bytesToAccept);
}

int progressCallback(
    void *userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    auto *state = static_cast<BlockingProgressState *>(userdata);
    if (!state || !state->callback) {
        return 0;
    }

    const QCTransferProgress progress(static_cast<qint64>(dlnow),
                                      static_cast<qint64>(dltotal),
                                      static_cast<qint64>(ulnow),
                                      static_cast<qint64>(ultotal));
    if (state->callback(progress, state->userData)) {
        return 0;
    }

    state->failureMessage = QStringLiteral("Blocking Extras progress callback cancelled request");
    return 1;
}

size_t writeHeaderCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *sink = static_cast<HeaderSink *>(userdata);
    if (!sink || !sink->headers) {
        return 0;
    }

    qint64 totalSize = 0;
    if (!calculateCallbackBytes(size, nmemb, &totalSize)) {
        sink->failureMessage = QStringLiteral("Blocking Extras response header chunk is too large");
        return 0;
    }
    if (totalSize <= 0) {
        return 0;
    }
    if (totalSize > std::numeric_limits<qsizetype>::max()) {
        sink->failureMessage = QStringLiteral(
            "Blocking Extras response header exceeds Qt size limit");
        return 0;
    }

    QByteArray line(ptr, static_cast<qsizetype>(totalSize));
    line            = line.trimmed();
    const int colon = line.indexOf(':');
    if (colon > 0) {
        sink->headers->append({line.left(colon).trimmed(), line.mid(colon + 1).trimmed()});
    }
    return static_cast<size_t>(totalSize);
}

struct BlockingExecution
{
    QByteArray responseBody;
    QCBlockingNetworkResult::HeaderList responseHeaders;
    qint64 bytesReceived = 0;
    long httpStatus      = 0;
    CURLcode code        = CURLE_OK;
};

NetworkError requestBodyError(const QCBlockingRequestBodyReadState &readState)
{
    if (readState.failureMessage.contains(QStringLiteral("read failed"))
        || readState.failureMessage.contains(QStringLiteral("missing"))) {
        return NetworkError::InputDeviceError;
    }
    return NetworkError::ReplayNotSupported;
}

QCBlockingNetworkResult makeCurlFailure(CURLcode code, const QString &message, int httpStatus)
{
    auto result = QCBlockingNetworkResult::failure(fromCurlCode(static_cast<int>(code)),
                                                   message,
                                                   httpStatus);
    result.setDiagnosticCurlCode(static_cast<int>(code));
    return result;
}

QCBlockingNetworkResult finishBlockingResult(const BlockingExecution &execution)
{
    if (execution.httpStatus >= 400) {
        return QCBlockingNetworkResult::failure(fromHttpCode(execution.httpStatus),
                                                QStringLiteral("HTTP error %1")
                                                    .arg(execution.httpStatus),
                                                static_cast<int>(execution.httpStatus));
    }

    return QCBlockingNetworkResult::success(static_cast<int>(execution.httpStatus),
                                            execution.responseBody,
                                            execution.responseHeaders,
                                            extractCookieDelta(execution.responseHeaders),
                                            execution.bytesReceived);
}

QCBlockingNetworkResult executeBlockingRequest(const QCNetworkRequest &request,
                                               HttpMethod method,
                                               const QByteArray &customMethod,
                                               QCBlockingRequestBody body,
                                               const QCCookieSnapshot &cookies,
                                               const QCBlockingRequestOptions &options,
                                               QIODevice *downloadOutput,
                                               qint64 abortAfterBytes = -1)
{
    const auto availability = CurlFeatureProbe::instance().minimumRuntimeAvailability();
    if (!availability.supported) {
        return QCBlockingNetworkResult::failure(NetworkError::UnsupportedCapability,
                                                availability.reason);
    }

    CurlGlobalConstructor::instance();
    CURL *handle = curl_easy_init();
    if (!handle) {
        return QCBlockingNetworkResult::failure(NetworkError::InvalidRequest,
                                                QStringLiteral("Blocking Extras curl init failed"));
    }

    RequestOptionStorage storage;
    curl_slist *requestHeaders = nullptr;
    BlockingExecution execution;
    ResponseSink responseSink{&execution.responseBody,
                              downloadOutput,
                              downloadOutput ? -1 : options.maxInMemoryBodyBytes(),
                              0,
                              abortAfterBytes,
                              QString(),
                              false};
    HeaderSink headerSink{&execution.responseHeaders, QString()};
    BlockingProgressState progressState{options.progressCallback(),
                                        options.progressCallbackUserData(),
                                        QString()};
    QCBlockingRequestBodyReadState readState{std::move(body), 0, QString()};

    const auto cleanup = qScopeGuard([&]() {
        if (requestHeaders) {
            curl_slist_free_all(requestHeaders);
        }
        if (storage.resolveList) {
            curl_slist_free_all(storage.resolveList);
        }
        if (storage.connectToList) {
            curl_slist_free_all(storage.connectToList);
        }
        curl_easy_cleanup(handle);
    });

    if (!configureRequestOptions(handle, request, &storage)
        || !appendRequestHeaders(handle, request, &requestHeaders)) {
        const QString errorMessage
            = storage.failureMessage.isEmpty()
                  ? QStringLiteral("Blocking Extras request option configuration failed")
                  : storage.failureMessage;
        return QCBlockingNetworkResult::failure(storage.unsupportedCapability
                                                    ? NetworkError::UnsupportedCapability
                                                    : NetworkError::InvalidRequest,
                                                errorMessage);
    }

    const QByteArray cookieHeader = cookieHeaderValue(cookies);
    if (!cookieHeader.isEmpty()) {
        curl_easy_setopt(handle, CURLOPT_COOKIE, cookieHeader.constData());
    }
    if (CurlOptions::setEnabled(handle, CURLOPT_NOSIGNAL, true) != CURLE_OK) {
        return QCBlockingNetworkResult::failure(
            NetworkError::InvalidRequest,
            QStringLiteral("Blocking Extras failed to set CURLOPT_NOSIGNAL"));
    }
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writeBodyCallback);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &responseSink);
    curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, writeHeaderCallback);
    curl_easy_setopt(handle, CURLOPT_HEADERDATA, &headerSink);
    if (progressState.callback) {
        curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, progressCallback);
        curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &progressState);
        if (CurlOptions::setEnabled(handle, CURLOPT_NOPROGRESS, false) != CURLE_OK) {
            return QCBlockingNetworkResult::failure(
                NetworkError::InvalidRequest,
                QStringLiteral("Blocking Extras failed to set CURLOPT_NOPROGRESS"));
        }
    }
    if (!configureBlockingCurlMethod(handle, method, customMethod, &readState)) {
        return QCBlockingNetworkResult::failure(NetworkError::InvalidRequest,
                                                readState.failureMessage);
    }

    execution.code          = curl_easy_perform(handle);
    execution.bytesReceived = responseSink.bytesReceived;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &execution.httpStatus);

    if (execution.code != CURLE_OK) {
        if (!readState.failureMessage.isEmpty()) {
            return QCBlockingNetworkResult::failure(requestBodyError(readState),
                                                    readState.failureMessage,
                                                    static_cast<int>(execution.httpStatus));
        }
        if (responseSink.cancelledByProgress) {
            return QCBlockingNetworkResult::failure(
                NetworkError::OperationCancelled,
                QStringLiteral(
                    "Blocking Extras download cancelled after writing requested byte limit"),
                static_cast<int>(execution.httpStatus));
        }
        if (!responseSink.failureMessage.isEmpty()) {
            return QCBlockingNetworkResult::failure(downloadOutput ? NetworkError::OutputDeviceError
                                                                   : NetworkError::BodyTooLarge,
                                                    responseSink.failureMessage,
                                                    static_cast<int>(execution.httpStatus));
        }
        if (!headerSink.failureMessage.isEmpty()) {
            return QCBlockingNetworkResult::failure(NetworkError::CallbackError,
                                                    headerSink.failureMessage,
                                                    static_cast<int>(execution.httpStatus));
        }
        if (!progressState.failureMessage.isEmpty()) {
            return QCBlockingNetworkResult::failure(NetworkError::OperationCancelled,
                                                    progressState.failureMessage,
                                                    static_cast<int>(execution.httpStatus));
        }
        return makeCurlFailure(execution.code,
                               QString::fromUtf8(curl_easy_strerror(execution.code)),
                               static_cast<int>(execution.httpStatus));
    }

    return finishBlockingResult(execution);
}

} // namespace

QCBlockingNetworkResult performBlockingRequest(const QCNetworkRequest &request,
                                               HttpMethod method,
                                               const QByteArray &body,
                                               const QCBlockingRequestOptions &options)
{
    return performBlockingRequest(request, method, makeBlockingBytesBody(body), options);
}

QCBlockingNetworkResult performBlockingRequest(const QCNetworkRequest &request,
                                               HttpMethod method,
                                               QCBlockingRequestBody body,
                                               const QCBlockingRequestOptions &options)
{
    return performBlockingRequest(request, method, std::move(body), QCCookieSnapshot(), options);
}

QCBlockingNetworkResult performBlockingRequest(const QCNetworkRequest &request,
                                               HttpMethod method,
                                               QCBlockingRequestBody body,
                                               const QCCookieSnapshot &cookies,
                                               const QCBlockingRequestOptions &options)
{
    return executeBlockingRequest(request,
                                  method,
                                  QByteArray(),
                                  std::move(body),
                                  cookies,
                                  options,
                                  nullptr);
}

QCBlockingNetworkResult performBlockingCustomRequest(const QCNetworkRequest &request,
                                                     QByteArrayView method,
                                                     QCBlockingRequestBody body,
                                                     const QCCookieSnapshot &cookies,
                                                     const QCBlockingRequestOptions &options)
{
    return executeBlockingRequest(request,
                                  HttpMethod::Custom,
                                  method.toByteArray(),
                                  std::move(body),
                                  cookies,
                                  options,
                                  nullptr);
}

QCBlockingNetworkResult performBlockingDownloadToDevice(const QCNetworkRequest &request,
                                                        HttpMethod method,
                                                        QCBlockingRequestBody body,
                                                        QIODevice *output,
                                                        const QCBlockingRequestOptions &options,
                                                        qint64 abortAfterBytes)
{
    return executeBlockingRequest(request,
                                  method,
                                  QByteArray(),
                                  std::move(body),
                                  QCCookieSnapshot(),
                                  options,
                                  output,
                                  abortAfterBytes);
}

} // namespace QCurl::Internal
