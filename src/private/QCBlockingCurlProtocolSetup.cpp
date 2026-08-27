/**
 * @file
 * @brief 实现 Blocking Extras 的 Core 协议白名单配置。
 */

#include "QCNetworkRequest.h"
#include "private/QCBlockingCurlProtocolSetup_p.h"
#include "private/QCBlockingCurlRequestSetup_p.h"
#include "private/QCBlockingHandleBridge_p.h"
#include "private/QCCurlOptionAdapter_p.h"

#include <QStringList>

namespace QCurl::Internal {
namespace {

bool setProtocolListOption(CURL *handle,
                           RequestOptionStorage *storage,
                           CURLoption option,
                           const char *optionName,
                           const QStringList &protocols,
                           QByteArray *bytes)
{
#ifdef QCURL_ENABLE_TEST_HOOKS
    const QList<QByteArray> forcedOptions = qgetenv("QCURL_TEST_FORCE_CAPABILITY_ERROR").split(',');
    for (const QByteArray &forced : forcedOptions) {
        const QByteArray trimmed = forced.trimmed();
        if (trimmed == "1" || trimmed == "all" || trimmed == optionName) {
            storage->failureMessage        = QStringLiteral(
                                                 "Blocking Extras Core protocol policy requires %1, but "
                                                 "this libcurl does not support it")
                                                 .arg(QString::fromUtf8(optionName));
            storage->unsupportedCapability = true;
            return false;
        }
    }
#endif

    *bytes = protocols.join(QLatin1Char(',')).toUtf8();
    const CURLcode rc = CurlOptions::setWithTestHook(handle, option, optionName, bytes->constData());
    if (rc == CURLE_OK) {
        return true;
    }

    if (rc == CURLE_UNKNOWN_OPTION || rc == CURLE_NOT_BUILT_IN) {
        storage->failureMessage        = QStringLiteral(
                                             "Blocking Extras Core protocol policy requires %1, but this "
                                             "libcurl does not support it: %2")
                                             .arg(QString::fromUtf8(optionName))
                                             .arg(QString::fromUtf8(curl_easy_strerror(rc)));
        storage->unsupportedCapability = true;
        return false;
    }

    storage->failureMessage = QStringLiteral("Blocking Extras failed to set %1: %2")
                                  .arg(QString::fromUtf8(optionName))
                                  .arg(QString::fromUtf8(curl_easy_strerror(rc)));
    return false;
}

} // namespace

bool configureProtocolOptions(CURL *handle,
                              const QCNetworkRequest &request,
                              RequestOptionStorage *storage)
{
    QStringList allowedProtocols;
    if (!resolveBlockingInitialProtocols(request.allowedProtocols(),
                                         &allowedProtocols,
                                         &storage->failureMessage)) {
        return false;
    }

    if (!setProtocolListOption(handle,
                               storage,
                               CURLOPT_PROTOCOLS_STR,
                               "CURLOPT_PROTOCOLS_STR",
                               allowedProtocols,
                               &storage->allowedProtocols)) {
        return false;
    }

    QStringList redirectProtocols;
    if (!resolveBlockingRedirectProtocols(request.allowedRedirectProtocols(),
                                          &redirectProtocols,
                                          &storage->failureMessage)) {
        return false;
    }

    return setProtocolListOption(handle,
                                 storage,
                                 CURLOPT_REDIR_PROTOCOLS_STR,
                                 "CURLOPT_REDIR_PROTOCOLS_STR",
                                 redirectProtocols,
                                 &storage->allowedRedirectProtocols);
}

} // namespace QCurl::Internal
