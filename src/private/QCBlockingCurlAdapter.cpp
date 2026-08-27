#include "QCNetworkError.h"
#include "private/QCBlockingHandleBridge_p.h"
#include "private/QCBlockingCurlAdapter_p.h"
#include "private/QCBlockingCurlMethodSetup_p.h"
#include "private/QCBlockingCurlRequestSetup_p.h"
#include "private/QCBlockingResponseSink_p.h"
#include "private/QCCurlOptionAdapter_p.h"

#include <QIODevice>

#include <curl/curl.h>
#include <optional>
#include <utility>

namespace QCurl::Internal {
namespace {

int progressCallback(
    void *userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    return invokeBlockingProgress(userdata,
                                  static_cast<qint64>(dltotal),
                                  static_cast<qint64>(dlnow),
                                  static_cast<qint64>(ultotal),
                                  static_cast<qint64>(ulnow));
}

/// 保存一次阻塞请求执行累计的响应数据和 libcurl 结果。
struct BlockingExecution
{
    QByteArray responseBody;
    QCBlockingNetworkResult::HeaderList responseHeaders;
    qint64 bytesReceived = 0;
    long httpStatus      = 0;
    CURLcode code        = CURLE_OK;
};

struct BlockingRequestContext
{
    BlockingRequestContext(CURL *curlHandle,
                           QCBlockingRequestBody body,
                           const QCBlockingRequestOptions &options,
                           QIODevice *output,
                           qint64 abortAfterBytes)
        : handle(curlHandle)
        , responseSink{&execution.responseBody,
                       output,
                       output ? -1 : options.maxInMemoryBodyBytes(),
                       0,
                       abortAfterBytes,
                       QString(),
                       false}
        , headerSink{&execution.responseHeaders, QString()}
        , progressState{options.progressCallback(), options.progressCallbackUserData(), QString()}
        , readState{std::move(body), 0, QString()}
        , downloadOutput(output)
    {}

    ~BlockingRequestContext()
    {
        if (requestHeaders) {
            curl_slist_free_all(requestHeaders);
        }
        if (storage.resolveList) {
            curl_slist_free_all(storage.resolveList);
        }
        if (storage.connectToList) {
            curl_slist_free_all(storage.connectToList);
        }
    }

    CURL *handle = nullptr;
    RequestOptionStorage storage;
    curl_slist *requestHeaders = nullptr;
    BlockingExecution execution;
    QCBlockingResponseSink responseSink;
    QCBlockingHeaderSink headerSink;
    QCBlockingProgressState progressState;
    QCBlockingRequestBodyReadState readState;
    QIODevice *downloadOutput = nullptr;
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

template<typename T>
bool setRequiredOption(BlockingRequestContext *context,
                       CURLoption option,
                       const char *optionName,
                       T value)
{
    const CURLcode code = CurlOptions::setWithTestHook(context->handle, option, optionName, value);
    if (code == CURLE_OK) {
        return true;
    }
    context->storage.failureMessage = QStringLiteral("Blocking Extras failed to set %1: %2")
                                          .arg(QString::fromUtf8(optionName))
                                          .arg(QString::fromUtf8(curl_easy_strerror(code)));
    return false;
}

QCBlockingNetworkResult optionFailure(const BlockingRequestContext &context)
{
    return QCBlockingNetworkResult::failure(NetworkError::InvalidRequest,
                                            context.storage.failureMessage);
}

std::optional<QCBlockingNetworkResult> configureRequestAndHeaders(BlockingRequestContext *context,
                                                                  const QCNetworkRequest &request)
{
    if (configureRequestOptions(context->handle, request, &context->storage)
        && appendRequestHeaders(context->handle,
                                request,
                                &context->requestHeaders,
                                &context->storage.failureMessage)) {
        return std::nullopt;
    }

    const QString errorMessage = context->storage.failureMessage.isEmpty()
                                     ? QStringLiteral(
                                           "Blocking Extras request option configuration failed")
                                     : context->storage.failureMessage;
    return QCBlockingNetworkResult::failure(context->storage.unsupportedCapability
                                                ? NetworkError::UnsupportedCapability
                                                : NetworkError::InvalidRequest,
                                            errorMessage);
}

std::optional<QCBlockingNetworkResult> configureResponseCallbacks(BlockingRequestContext *context,
                                                                  const QCCookieSnapshot &cookies)
{
    const QByteArray cookieHeader = cookieHeaderValue(cookies);
    if (!cookieHeader.isEmpty()
        && !setRequiredOption(context, CURLOPT_COOKIE, "CURLOPT_COOKIE", cookieHeader.constData())) {
        return optionFailure(*context);
    }

    if (!setRequiredOption(context,
                           CURLOPT_WRITEFUNCTION,
                           "CURLOPT_WRITEFUNCTION",
                           writeBlockingResponseBody)
        || !setRequiredOption(context, CURLOPT_WRITEDATA, "CURLOPT_WRITEDATA", &context->responseSink)
        || !setRequiredOption(context,
                              CURLOPT_HEADERFUNCTION,
                              "CURLOPT_HEADERFUNCTION",
                              writeBlockingResponseHeader)
        || !setRequiredOption(context,
                              CURLOPT_HEADERDATA,
                              "CURLOPT_HEADERDATA",
                              &context->headerSink)) {
        return optionFailure(*context);
    }
    return std::nullopt;
}

std::optional<QCBlockingNetworkResult> configureProgressCallback(BlockingRequestContext *context)
{
    if (!context->progressState.callback) {
        return std::nullopt;
    }
    if (!setRequiredOption(context,
                           CURLOPT_XFERINFOFUNCTION,
                           "CURLOPT_XFERINFOFUNCTION",
                           progressCallback)
        || !setRequiredOption(context,
                              CURLOPT_XFERINFODATA,
                              "CURLOPT_XFERINFODATA",
                              &context->progressState)) {
        return optionFailure(*context);
    }
    if (CurlOptions::setEnabled(context->handle, CURLOPT_NOPROGRESS, false) != CURLE_OK) {
        return QCBlockingNetworkResult::failure(
            NetworkError::InvalidRequest,
            QStringLiteral("Blocking Extras failed to set CURLOPT_NOPROGRESS"));
    }
    return std::nullopt;
}

QCBlockingNetworkResult curlExecutionFailure(const BlockingRequestContext &context)
{
    const int httpStatus = static_cast<int>(context.execution.httpStatus);
    if (!context.readState.failureMessage.isEmpty()) {
        return QCBlockingNetworkResult::failure(requestBodyError(context.readState),
                                                context.readState.failureMessage,
                                                httpStatus);
    }
    if (context.responseSink.cancelledByProgress) {
        return QCBlockingNetworkResult::failure(
            NetworkError::OperationCancelled,
            QStringLiteral("Blocking Extras download cancelled after writing requested byte limit"),
            httpStatus);
    }
    if (!context.responseSink.failureMessage.isEmpty()) {
        return QCBlockingNetworkResult::failure(context.downloadOutput
                                                    ? NetworkError::OutputDeviceError
                                                    : NetworkError::BodyTooLarge,
                                                context.responseSink.failureMessage,
                                                httpStatus);
    }
    if (!context.headerSink.failureMessage.isEmpty()) {
        return QCBlockingNetworkResult::failure(NetworkError::CallbackError,
                                                context.headerSink.failureMessage,
                                                httpStatus);
    }
    if (!context.progressState.failureMessage.isEmpty()) {
        return QCBlockingNetworkResult::failure(NetworkError::OperationCancelled,
                                                context.progressState.failureMessage,
                                                httpStatus);
    }
    return makeCurlFailure(context.execution.code,
                           QString::fromUtf8(curl_easy_strerror(context.execution.code)),
                           httpStatus);
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
    QString protocolError;
    if (!validateBlockingUrl(request.url(), &protocolError)) {
        return QCBlockingNetworkResult::failure(NetworkError::InvalidRequest, protocolError);
    }

    const auto availability = blockingRuntimeAvailability();
    if (!availability.supported) {
        return QCBlockingNetworkResult::failure(NetworkError::UnsupportedCapability,
                                                availability.reason);
    }

    QCBlockingHandleBridge curlManager;
    CURL *handle = curlManager.handle();
    if (!handle) {
        return QCBlockingNetworkResult::failure(NetworkError::InvalidRequest,
                                                curlManager.initializationError());
    }

    BlockingRequestContext context(handle, std::move(body), options, downloadOutput, abortAfterBytes);
    if (const auto failure = configureRequestAndHeaders(&context, request)) {
        return *failure;
    }
    if (const auto failure = configureResponseCallbacks(&context, cookies)) {
        return *failure;
    }
    if (const auto failure = configureProgressCallback(&context)) {
        return *failure;
    }
    if (!configureBlockingCurlMethod(handle, method, customMethod, &context.readState)) {
        return QCBlockingNetworkResult::failure(NetworkError::InvalidRequest,
                                                context.readState.failureMessage);
    }

    context.execution.code          = curl_easy_perform(handle);
    context.execution.bytesReceived = context.responseSink.bytesReceived;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &context.execution.httpStatus);

    if (context.execution.code != CURLE_OK) {
        return curlExecutionFailure(context);
    }

    return finishBlockingResult(context.execution);
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
