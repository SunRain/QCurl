/**
 * @file
 * @brief QCNetworkReply connection-pool and HTTP-version curl options.
 */

#include "CurlFeatureProbe.h"
#include "QCNetworkConnectionPoolManager_p.h"
#include "QCNetworkError.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkReply_p.h"
#include "QCNetworkRequest.h"
#include "private/QCNetworkHttpVersion_p.h"
#include "private/QCNetworkReplyCurlOptions_p.h"

namespace QCurl::Internal::ReplyCurlOptions {

using namespace Detail;

[[nodiscard]] bool configureReplyHttpVersionOptions(QCNetworkReplyPrivate *reply,
                                                    CURL *handle,
                                                    const QCNetworkRequest &request)
{
    const QCNetworkHttpVersion requested = request.httpVersion();
    QCNetworkHttpVersion effective       = requested;
    QString error;
    QString warning;
    if (!detail::resolveHttpVersion(requested, &effective, &error, &warning)) {
        reply->setError(NetworkError::InvalidRequest, error);
        return false;
    }
    if (!warning.isEmpty()) {
        appendCapabilityWarning(reply, warning);
    }

    return (effective == QCNetworkHttpVersion::Http1_1 && !request.isHttpVersionExplicit())
           || setRequiredCurlOption(reply,
                                    handle,
                                    CURLOPT_HTTP_VERSION,
                                    "CURLOPT_HTTP_VERSION",
                                    detail::toCurlHttpVersion(effective));
}

} // namespace QCurl::Internal::ReplyCurlOptions
