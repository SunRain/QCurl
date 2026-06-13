#include "private/QCBlockingCurlMethodSetup_p.h"
#include "private/QCCurlOptionAdapter_p.h"

#include <QString>

namespace QCurl::Internal {
namespace {

bool setBlockingCurlOption(CURLcode code,
                           QCBlockingRequestBodyReadState *readState,
                           const QString &optionName)
{
    if (code == CURLE_OK) {
        return true;
    }

    readState->failureMessage = QStringLiteral("Blocking Extras failed to set %1: %2")
                                    .arg(optionName)
                                    .arg(QString::fromUtf8(curl_easy_strerror(code)));
    return false;
}

bool configureUploadMethod(CURL *handle,
                           const char *methodName,
                           QCBlockingRequestBodyReadState *readState)
{
    return setBlockingCurlOption(CurlOptions::setEnabled(handle, CURLOPT_UPLOAD, true),
                                 readState,
                                 QStringLiteral("CURLOPT_UPLOAD"))
           && setBlockingCurlOption(curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, methodName),
                                    readState,
                                    QStringLiteral("CURLOPT_CUSTOMREQUEST"))
           && setBlockingCurlOption(curl_easy_setopt(handle,
                                                     CURLOPT_READFUNCTION,
                                                     readBlockingRequestBodyCallback),
                                    readState,
                                    QStringLiteral("CURLOPT_READFUNCTION"))
           && setBlockingCurlOption(curl_easy_setopt(handle, CURLOPT_READDATA, readState),
                                    readState,
                                    QStringLiteral("CURLOPT_READDATA"))
           && setBlockingCurlOption(curl_easy_setopt(handle,
                                                     CURLOPT_SEEKFUNCTION,
                                                     seekBlockingRequestBodyCallback),
                                    readState,
                                    QStringLiteral("CURLOPT_SEEKFUNCTION"))
           && setBlockingCurlOption(curl_easy_setopt(handle, CURLOPT_SEEKDATA, readState),
                                    readState,
                                    QStringLiteral("CURLOPT_SEEKDATA"))
           && setBlockingCurlOption(curl_easy_setopt(handle,
                                                     CURLOPT_INFILESIZE_LARGE,
                                                     curlBodySize(readState->body)),
                                    readState,
                                    QStringLiteral("CURLOPT_INFILESIZE_LARGE"));
}

} // namespace

bool configureBlockingCurlMethod(CURL *handle,
                                 HttpMethod method,
                                 const QByteArray &customMethod,
                                 QCBlockingRequestBodyReadState *readState)
{
    switch (method) {
        case HttpMethod::Head:
            return setBlockingCurlOption(CurlOptions::setEnabled(handle, CURLOPT_NOBODY, true),
                                         readState,
                                         QStringLiteral("CURLOPT_NOBODY"));
        case HttpMethod::Get:
            return setBlockingCurlOption(CurlOptions::setEnabled(handle, CURLOPT_HTTPGET, true),
                                         readState,
                                         QStringLiteral("CURLOPT_HTTPGET"));
        case HttpMethod::Post:
            if (isStreamingBody(readState->body)) {
                return configureUploadMethod(handle, "POST", readState);
            }
            return setBlockingCurlOption(CurlOptions::setEnabled(handle, CURLOPT_POST, true),
                                         readState,
                                         QStringLiteral("CURLOPT_POST"))
                   && setBlockingCurlOption(curl_easy_setopt(handle,
                                                             CURLOPT_POSTFIELDS,
                                                             readState->body.bytes->constData()),
                                            readState,
                                            QStringLiteral("CURLOPT_POSTFIELDS"))
                   && setBlockingCurlOption(curl_easy_setopt(handle,
                                                             CURLOPT_POSTFIELDSIZE_LARGE,
                                                             curlBodySize(readState->body)),
                                            readState,
                                            QStringLiteral("CURLOPT_POSTFIELDSIZE_LARGE"));
        case HttpMethod::Put:
            return configureUploadMethod(handle, "PUT", readState);
        case HttpMethod::Patch:
            if (isStreamingBody(readState->body)) {
                return configureUploadMethod(handle, "PATCH", readState);
            }
            return setBlockingCurlOption(curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "PATCH"),
                                         readState,
                                         QStringLiteral("CURLOPT_CUSTOMREQUEST"))
                   && setBlockingCurlOption(curl_easy_setopt(handle,
                                                             CURLOPT_POSTFIELDS,
                                                             readState->body.bytes->constData()),
                                            readState,
                                            QStringLiteral("CURLOPT_POSTFIELDS"))
                   && setBlockingCurlOption(curl_easy_setopt(handle,
                                                             CURLOPT_POSTFIELDSIZE_LARGE,
                                                             curlBodySize(readState->body)),
                                            readState,
                                            QStringLiteral("CURLOPT_POSTFIELDSIZE_LARGE"));
        case HttpMethod::Delete:
            return setBlockingCurlOption(curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "DELETE"),
                                         readState,
                                         QStringLiteral("CURLOPT_CUSTOMREQUEST"));
        case HttpMethod::Custom:
            if (!setBlockingCurlOption(curl_easy_setopt(handle,
                                                        CURLOPT_CUSTOMREQUEST,
                                                        customMethod.constData()),
                                       readState,
                                       QStringLiteral("CURLOPT_CUSTOMREQUEST"))) {
                return false;
            }
            if (isStreamingBody(readState->body)) {
                return setBlockingCurlOption(CurlOptions::setEnabled(handle, CURLOPT_UPLOAD, true),
                                             readState,
                                             QStringLiteral("CURLOPT_UPLOAD"))
                       && setBlockingCurlOption(curl_easy_setopt(handle,
                                                                 CURLOPT_READFUNCTION,
                                                                 readBlockingRequestBodyCallback),
                                                readState,
                                                QStringLiteral("CURLOPT_READFUNCTION"))
                       && setBlockingCurlOption(curl_easy_setopt(handle,
                                                                 CURLOPT_READDATA,
                                                                 readState),
                                                readState,
                                                QStringLiteral("CURLOPT_READDATA"))
                       && setBlockingCurlOption(curl_easy_setopt(handle,
                                                                 CURLOPT_SEEKFUNCTION,
                                                                 seekBlockingRequestBodyCallback),
                                                readState,
                                                QStringLiteral("CURLOPT_SEEKFUNCTION"))
                       && setBlockingCurlOption(curl_easy_setopt(handle,
                                                                 CURLOPT_SEEKDATA,
                                                                 readState),
                                                readState,
                                                QStringLiteral("CURLOPT_SEEKDATA"))
                       && setBlockingCurlOption(curl_easy_setopt(handle,
                                                                 CURLOPT_INFILESIZE_LARGE,
                                                                 curlBodySize(readState->body)),
                                                readState,
                                                QStringLiteral("CURLOPT_INFILESIZE_LARGE"));
            } else if (curlBodySize(readState->body) > 0) {
                return setBlockingCurlOption(curl_easy_setopt(handle,
                                                              CURLOPT_POSTFIELDS,
                                                              readState->body.bytes->constData()),
                                             readState,
                                             QStringLiteral("CURLOPT_POSTFIELDS"))
                       && setBlockingCurlOption(curl_easy_setopt(handle,
                                                                 CURLOPT_POSTFIELDSIZE_LARGE,
                                                                 curlBodySize(readState->body)),
                                                readState,
                                                QStringLiteral("CURLOPT_POSTFIELDSIZE_LARGE"));
            }
            return true;
    }

    return true;
}

} // namespace QCurl::Internal
