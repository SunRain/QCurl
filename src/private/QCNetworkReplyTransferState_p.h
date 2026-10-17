/**
 * @file
 * @brief 保存 multi transfer record 拥有的异步传输状态。
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
#include <memory>

namespace QCurl {

struct QCCurlMultiDomain;

/**
 * @brief 保存活动 easy handle 借用的回调数据与选项存储。
 *
 * Reply 存活期间观察本状态；easy handle 进入 multi 后，transfer record 也持有本状态，
 * 直至 detach 完成。两个原生列表分别持有，不能在仍被 libcurl 使用时释放。
 */
struct QCNetworkReplyTransferState
{
    ~QCNetworkReplyTransferState()
    {
        curl_slist_free_all(resolveSlist);
        curl_slist_free_all(connectToSlist);
    }

    Internal::CurlPlan curlPlan;
    // 重试等待期间固定原域；终态归还。活动 record 另持有引用直到原生 detach 成功。
    std::shared_ptr<QCCurlMultiDomain> transportDomain;

    ReplyState state = ReplyState::Idle;
    QCByteDataBuffer bodyBuffer;
    QByteArray cacheBodyBuffer;
    qint64 cacheBodyLimit = -1; ///< -1 表示本次响应不再收集缓存候选。
    QByteArray headerData;
    QList<RawHeaderPair> finalHeaderList;
    QMap<QByteArray, QByteArray> finalHeaderMap;
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
    curl_slist *resolveSlist   = nullptr; ///< CURLOPT_RESOLVE 原始规范化列表。
    curl_slist *connectToSlist = nullptr; ///< 独立的 CURLOPT_CONNECT_TO 列表。

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
