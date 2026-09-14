/**
 * @file
 * @brief QCNetworkReply timeout and callback curl options.
 */

#include "CurlFeatureProbe.h"
#include "QCNetworkConnectionPoolManager_p.h"
#include "QCNetworkError.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkReply_p.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRetryPolicy.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkTimeoutConfig.h"
#include "private/QCNetworkHttpVersion_p.h"
#include "private/QCNetworkReplyCurlOptions_p.h"
#include "private/QCNetworkReplyRuntime_p.h"

#include <QDebug>
#include <QFileInfo>
#include <QIODevice>
#include <QUrl>

#include <limits>
#include <optional>

namespace QCurl::Internal::ReplyCurlOptions {

using namespace Detail;

namespace {

[[nodiscard]] bool configureTimeouts(QCNetworkReplyPrivate *reply,
                                     CURL *handle,
                                     const QCNetworkTimeoutConfig &timeout)
{
    if (timeout.connectTimeout().has_value() && timeout.connectTimeout()->count() > 0) {
        if (!setRequiredOption(reply,
                               Internal::CurlOptions::setConnectTimeout(handle,
                                                                        timeout.connectTimeout()
                                                                            .value()),
                               QCURL_CURL_OPTION(CURLOPT_CONNECTTIMEOUT_MS).name)) {
            return false;
        }
    }

    if (timeout.totalTimeout().has_value() && timeout.totalTimeout()->count() > 0) {
        if (!setRequiredCurlOption(reply,
                                   handle,
                                   QCURL_CURL_OPTION(CURLOPT_TIMEOUT_MS),
                                   static_cast<long>(timeout.totalTimeout()->count()))) {
            return false;
        }
    }

    if (timeout.lowSpeedTime().has_value() && timeout.lowSpeedTime()->count() > 0) {
        if (!setRequiredCurlOption(reply,
                                   handle,
                                   QCURL_CURL_OPTION(CURLOPT_LOW_SPEED_TIME),
                                   static_cast<long>(timeout.lowSpeedTime()->count()))) {
            return false;
        }
    }

    if (timeout.lowSpeedLimit().has_value() && *timeout.lowSpeedLimit() > 0) {
        if (!setRequiredCurlOption(reply,
                                   handle,
                                   QCURL_CURL_OPTION(CURLOPT_LOW_SPEED_LIMIT),
                                   static_cast<long>(*timeout.lowSpeedLimit()))) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool configureResponseCallbacks(QCNetworkReplyPrivate *reply, CURL *handle)
{
    if (!setRequiredCurlOption(reply,
                               handle,
                               QCURL_CURL_OPTION(CURLOPT_WRITEFUNCTION),
                               QCNetworkReplyPrivate::curlWriteCallback)
        || !setRequiredCurlOption(reply, handle, QCURL_CURL_OPTION(CURLOPT_WRITEDATA), reply)) {
        return false;
    }

    if (!setRequiredCurlOption(reply,
                               handle,
                               QCURL_CURL_OPTION(CURLOPT_HEADERFUNCTION),
                               QCNetworkReplyPrivate::curlHeaderCallback)
        || !setRequiredCurlOption(reply, handle, QCURL_CURL_OPTION(CURLOPT_HEADERDATA), reply)) {
        return false;
    }

    return true;
}

[[nodiscard]] bool configureTransferCallbacks(QCNetworkReplyPrivate *reply, CURL *handle)
{
    if (!setRequiredCurlOption(reply,
                               handle,
                               QCURL_CURL_OPTION(CURLOPT_SEEKFUNCTION),
                               QCNetworkReplyPrivate::curlSeekCallback)
        || !setRequiredCurlOption(reply, handle, QCURL_CURL_OPTION(CURLOPT_SEEKDATA), reply)) {
        return false;
    }

    if (!setRequiredCurlOption(reply,
                               handle,
                               QCURL_CURL_OPTION(CURLOPT_XFERINFOFUNCTION),
                               QCNetworkReplyPrivate::curlProgressCallback)
        || !setRequiredCurlOption(reply, handle, QCURL_CURL_OPTION(CURLOPT_XFERINFODATA), reply)) {
        return false;
    }
    if (!setRequiredOption(reply,
                           Internal::CurlOptions::setEnabled(handle, CURLOPT_NOPROGRESS, false),
                           QCURL_CURL_OPTION(CURLOPT_NOPROGRESS).name)) {
        return false;
    }

    return true;
}

} // namespace

[[nodiscard]] bool configureReplyTimeoutsAndCallbacks(QCNetworkReplyPrivate *reply,
                                                      CURL *handle,
                                                      const QCNetworkRequest &request)
{
    return configureTimeouts(reply, handle, request.timeoutConfig())
           && configureResponseCallbacks(reply, handle)
           && configureTransferCallbacks(reply, handle);
}

} // namespace QCurl::Internal::ReplyCurlOptions
