#include "private/QCBlockingCurlMethodSetup_p.h"
#include "private/QCCurlOptionAdapter_p.h"

#include <QString>

namespace QCurl::Internal {
namespace {

template<typename T>
[[nodiscard]] bool setBlockingCurlOption(CURL *handle,
                                         QCBlockingRequestBodyReadState *readState,
                                         CurlOptions::Option option,
                                         T value)
{
    const CURLcode code = curl_easy_setopt(handle, option.id, value);
    if (code == CURLE_OK) {
        return true;
    }

    readState->failureMessage = QStringLiteral("Blocking Extras failed to set %1: %2")
                                    .arg(QString::fromLatin1(option.name))
                                    .arg(QString::fromUtf8(curl_easy_strerror(code)));
    return false;
}

bool configureUploadCallbacks(CURL *handle, QCBlockingRequestBodyReadState *readState)
{
    return setBlockingCurlOption(handle,
                                 readState,
                                 QCURL_CURL_OPTION(CURLOPT_READFUNCTION),
                                 readBlockingRequestBodyCallback)
           && setBlockingCurlOption(handle,
                                    readState,
                                    QCURL_CURL_OPTION(CURLOPT_READDATA),
                                    readState)
           && setBlockingCurlOption(handle,
                                    readState,
                                    QCURL_CURL_OPTION(CURLOPT_SEEKFUNCTION),
                                    seekBlockingRequestBodyCallback)
           && setBlockingCurlOption(handle,
                                    readState,
                                    QCURL_CURL_OPTION(CURLOPT_SEEKDATA),
                                    readState)
           && setBlockingCurlOption(handle,
                                    readState,
                                    QCURL_CURL_OPTION(CURLOPT_INFILESIZE_LARGE),
                                    curlBodySize(readState->body));
}

bool configureUploadMethod(CURL *handle,
                           const char *methodName,
                           QCBlockingRequestBodyReadState *readState)
{
    return setBlockingCurlOption(handle,
                                 readState,
                                 QCURL_CURL_OPTION(CURLOPT_UPLOAD),
                                 CurlOptions::kEnabled)
           && setBlockingCurlOption(handle,
                                    readState,
                                    QCURL_CURL_OPTION(CURLOPT_CUSTOMREQUEST),
                                    methodName)
           && configureUploadCallbacks(handle, readState);
}

bool configureBufferedBody(CURL *handle, QCBlockingRequestBodyReadState *readState)
{
    return setBlockingCurlOption(handle,
                                 readState,
                                 QCURL_CURL_OPTION(CURLOPT_POSTFIELDS),
                                 readState->body.bytes->constData())
           && setBlockingCurlOption(handle,
                                    readState,
                                    QCURL_CURL_OPTION(CURLOPT_POSTFIELDSIZE_LARGE),
                                    curlBodySize(readState->body));
}

bool configurePostMethod(CURL *handle, QCBlockingRequestBodyReadState *readState)
{
    if (isStreamingBody(readState->body)) {
        return configureUploadMethod(handle, "POST", readState);
    }
    return setBlockingCurlOption(handle,
                                 readState,
                                 QCURL_CURL_OPTION(CURLOPT_POST),
                                 CurlOptions::kEnabled)
           && configureBufferedBody(handle, readState);
}

bool configurePatchMethod(CURL *handle, QCBlockingRequestBodyReadState *readState)
{
    if (isStreamingBody(readState->body)) {
        return configureUploadMethod(handle, "PATCH", readState);
    }
    return setBlockingCurlOption(handle,
                                 readState,
                                 QCURL_CURL_OPTION(CURLOPT_CUSTOMREQUEST),
                                 "PATCH")
           && configureBufferedBody(handle, readState);
}

bool configureCustomMethod(CURL *handle,
                           const QByteArray &method,
                           QCBlockingRequestBodyReadState *readState)
{
    if (!setBlockingCurlOption(handle,
                               readState,
                               QCURL_CURL_OPTION(CURLOPT_CUSTOMREQUEST),
                               method.constData())) {
        return false;
    }
    if (isStreamingBody(readState->body)) {
        return setBlockingCurlOption(handle,
                                     readState,
                                     QCURL_CURL_OPTION(CURLOPT_UPLOAD),
                                     CurlOptions::kEnabled)
               && configureUploadCallbacks(handle, readState);
    }
    return curlBodySize(readState->body) <= 0 || configureBufferedBody(handle, readState);
}

} // namespace

bool configureBlockingCurlMethod(CURL *handle,
                                 HttpMethod method,
                                 const QByteArray &customMethod,
                                 QCBlockingRequestBodyReadState *readState)
{
    switch (method) {
        case HttpMethod::Head:
            return setBlockingCurlOption(handle,
                                         readState,
                                         QCURL_CURL_OPTION(CURLOPT_NOBODY),
                                         CurlOptions::kEnabled);
        case HttpMethod::Get:
            return setBlockingCurlOption(handle,
                                         readState,
                                         QCURL_CURL_OPTION(CURLOPT_HTTPGET),
                                         CurlOptions::kEnabled);
        case HttpMethod::Post:
            return configurePostMethod(handle, readState);
        case HttpMethod::Put:
            return configureUploadMethod(handle, "PUT", readState);
        case HttpMethod::Patch:
            return configurePatchMethod(handle, readState);
        case HttpMethod::Delete:
            return setBlockingCurlOption(handle,
                                         readState,
                                         QCURL_CURL_OPTION(CURLOPT_CUSTOMREQUEST),
                                         "DELETE");
        case HttpMethod::Custom:
            return configureCustomMethod(handle, customMethod, readState);
    }
    return true;
}

} // namespace QCurl::Internal
