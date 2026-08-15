/**
 * @file
 * @brief Active async transfer state owned by the multi transfer record.
 */

#ifndef QCNETWORKREPLYTRANSFERSTATE_P_H
#define QCNETWORKREPLYTRANSFERSTATE_P_H

#include "QCNetworkLogger.h"
#include "QCNetworkReply.h"
#include "private/QCNetworkReplyBodySource_p.h"
#include "private/QCRequestPipeline_p.h"
#include "qbytedata_p.h"

#include <QMap>
#include <QString>

#include <curl/curl.h>

namespace QCurl {

/**
 * @brief Callback data and option backing that must outlive an active easy handle.
 *
 * A reply observes this state while it exists. Once the easy handle enters a
 * multi handle, the transfer record also owns it until detach completes.
 */
struct QCNetworkReplyTransferState
{
    ~QCNetworkReplyTransferState()
    {
        curl_slist_free_all(resolveSlist);
        curl_slist_free_all(connectToSlist);
    }

    Internal::CurlPlan curlPlan;

    ReplyState state = ReplyState::Idle;
    QCByteDataBuffer bodyBuffer;
    QByteArray cacheBodyBuffer;
    QByteArray headerData;
    QList<RawHeaderPair> finalHeaderList;
    QMap<QByteArray, QByteArray> finalHeaderMap;
    QMap<QString, QString> headerMap;
    int httpStatusCode = 0;

    int userPauseMask                    = 0;
    int internalPauseMask                = 0;
    int appliedPauseMask                 = 0;
    qint64 backpressureLimitBytes        = 0;
    qint64 backpressureResumeBytes       = 0;
    qint64 backpressurePeakBufferedBytes = 0;
    bool backpressureActive              = false;
    bool uploadSendPaused                = false;

    qint64 bytesDownloaded = 0;
    qint64 bytesUploaded   = 0;
    qint64 downloadTotal   = -1;
    qint64 uploadTotal     = -1;

    QCNetworkLoggerHandle logger;
    bool debugTraceEnabled = false;

    QString cookieFilePath;
    int cookieMode = 0;
    QByteArray hstsCachePathBytes;
    QByteArray altSvcCachePathBytes;

    QByteArray proxyHostBytes;
    QByteArray proxyUserBytes;
    QByteArray proxyPasswordBytes;
    bool proxyEnvironmentDisabled = false;
    QByteArray httpAuthUserBytes;
    QByteArray httpAuthPasswordBytes;
    QByteArray refererBytes;
    QByteArray acceptEncodingBytes;

    QByteArray interfaceBytes;
    QByteArray dnsServersBytes;
    QByteArray dohUrlBytes;
    curl_slist *resolveSlist   = nullptr;
    curl_slist *connectToSlist = nullptr;

    QByteArray allowedProtocolsBytes;
    QByteArray allowedRedirectProtocolsBytes;
    QByteArray sslCaCertPathBytes;
    QByteArray sslClientCertPathBytes;
    QByteArray sslClientKeyPathBytes;
    QByteArray sslClientKeyPasswordBytes;
    QByteArray sslPinnedPublicKeyBytes;
    QByteArray sslCipherListBytes;
    QByteArray sslTls13CiphersBytes;
    QByteArray proxySslCaCertPathBytes;
    QByteArray proxySslCipherListBytes;
    QByteArray proxySslTls13CiphersBytes;

    Internal::ReplyBodySourceState requestBodySource;
};

} // namespace QCurl

#endif // QCNETWORKREPLYTRANSFERSTATE_P_H
