/**
 * @file
 * @brief 保存 reply、活动 record 与 detached handle 共享的异步传输状态。
 */

#ifndef QCNETWORKREPLYTRANSFERSTATE_P_H
#define QCNETWORKREPLYTRANSFERSTATE_P_H

#include "QCNetworkLogger.h"
#include "QCNetworkReply.h"
#include "private/QCNetworkReplyBodySource_p.h"
#include "private/QCNetworkReplyTrace_p.h"
#include "private/QCRequestPipeline_p.h"
#include "private/QCTlsConfiguration_p.h"
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
 * Reply 与活动 transfer record 共享本状态；detached handle 守卫继续持有同一状态直到
 * cleanup，防止 queued completion 被丢弃时先释放仍被 libcurl 借用的列表或请求体。
 */
struct Q_DECL_HIDDEN QCNetworkReplyTransferState
{
    explicit QCNetworkReplyTransferState(const QUrl &initialUrl,
                                         QCNetworkDiagnosticQueryPolicy policy)
        : trace(initialUrl, policy)
    {}

    ~QCNetworkReplyTransferState()
    {
        curl_slist_free_all(resolveSlist);
        curl_slist_free_all(connectToSlist);
    }

    Internal::CurlPlan curlPlan;
    // 重试等待期间固定原域；终态归还。活动 record 另持有引用直到原生 detach 成功。
    std::shared_ptr<QCCurlMultiDomain> transportDomain;
    // 仅在 add 提交后、detach 成功前于 owner thread 或允许的 libcurl 回调内借用。
    CURL *activeEasyHandle       = nullptr;
    bool readCallbackConfigured  = false;
    bool debugCallbackConfigured = false;

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
    Internal::ReplyTraceState trace;

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
    Internal::QCTlsConfiguration tlsConfiguration;

    Internal::ReplyBodySourceState requestBodySource;
};

} // namespace QCurl

#endif // QCNETWORKREPLYTRANSFERSTATE_P_H
