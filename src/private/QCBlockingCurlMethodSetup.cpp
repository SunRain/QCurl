#include "private/QCBlockingCurlMethodSetup_p.h"

namespace QCurl::Internal {
namespace {

void configureUploadMethod(CURL *handle,
                           const char *methodName,
                           QCBlockingRequestBodyReadState *readState)
{
    curl_easy_setopt(handle, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, methodName);
    curl_easy_setopt(handle, CURLOPT_READFUNCTION, readBlockingRequestBodyCallback);
    curl_easy_setopt(handle, CURLOPT_READDATA, readState);
    curl_easy_setopt(handle, CURLOPT_SEEKFUNCTION, seekBlockingRequestBodyCallback);
    curl_easy_setopt(handle, CURLOPT_SEEKDATA, readState);
    curl_easy_setopt(handle, CURLOPT_INFILESIZE_LARGE, curlBodySize(readState->body));
}

} // namespace

void configureBlockingCurlMethod(CURL *handle,
                                 HttpMethod method,
                                 const QByteArray &customMethod,
                                 QCBlockingRequestBodyReadState *readState)
{
    switch (method) {
        case HttpMethod::Head:
            curl_easy_setopt(handle, CURLOPT_NOBODY, 1L);
            break;
        case HttpMethod::Get:
            curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L);
            break;
        case HttpMethod::Post:
            if (isStreamingBody(readState->body)) {
                configureUploadMethod(handle, "POST", readState);
            } else {
                curl_easy_setopt(handle, CURLOPT_POST, 1L);
                curl_easy_setopt(handle, CURLOPT_POSTFIELDS, readState->body.bytes->constData());
                curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE, curlBodySize(readState->body));
            }
            break;
        case HttpMethod::Put:
            configureUploadMethod(handle, "PUT", readState);
            break;
        case HttpMethod::Patch:
            if (isStreamingBody(readState->body)) {
                configureUploadMethod(handle, "PATCH", readState);
            } else {
                curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "PATCH");
                curl_easy_setopt(handle, CURLOPT_POSTFIELDS, readState->body.bytes->constData());
                curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE, curlBodySize(readState->body));
            }
            break;
        case HttpMethod::Delete:
            curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "DELETE");
            break;
        case HttpMethod::Custom:
            curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, customMethod.constData());
            if (isStreamingBody(readState->body)) {
                curl_easy_setopt(handle, CURLOPT_UPLOAD, 1L);
                curl_easy_setopt(handle, CURLOPT_READFUNCTION, readBlockingRequestBodyCallback);
                curl_easy_setopt(handle, CURLOPT_READDATA, readState);
                curl_easy_setopt(handle, CURLOPT_SEEKFUNCTION, seekBlockingRequestBodyCallback);
                curl_easy_setopt(handle, CURLOPT_SEEKDATA, readState);
                curl_easy_setopt(handle, CURLOPT_INFILESIZE_LARGE, curlBodySize(readState->body));
            } else if (curlBodySize(readState->body) > 0) {
                curl_easy_setopt(handle, CURLOPT_POSTFIELDS, readState->body.bytes->constData());
                curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE, curlBodySize(readState->body));
            }
            break;
    }
}

} // namespace QCurl::Internal
