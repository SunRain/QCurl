#include "QCNetworkReply.h"

#include "QCCurlMultiManager.h"
#include "QCNetworkConnectionPoolManager_p.h"
#include "QCNetworkReply_p.h"
#include "private/QCCurlMultiTransferRecord_p.h"
#include "private/QCNetworkReplyBodySource_p.h"
#include "private/QCNetworkReplyRuntime_p.h"
#include "private/QCRequestPipeline_p.h"

#include <QDebug>
#include <QThread>
#include <QVariant>

namespace QCurl {

// ==================
// QCNetworkReplyPrivate 实现
// ==================

QCNetworkReplyPrivate::QCNetworkReplyPrivate(QCNetworkReply *q,
                                             const QCNetworkRequest &req,
                                             HttpMethod method,
                                             const Internal::RequestBody &requestBodySource,
                                             const QByteArray &body)
    : request(req)
    , httpMethod(method)
    , transferState(QSharedPointer<QCNetworkReplyTransferState>::create())
    , curlPlan(transferState->curlPlan)
    , multiProcessor(nullptr)
    , bodyBuffer(transferState->bodyBuffer)
    , cacheBodyBuffer(transferState->cacheBodyBuffer)
    , headerData(transferState->headerData)
    , finalHeaderList(transferState->finalHeaderList)
    , finalHeaderMap(transferState->finalHeaderMap)
    , state(transferState->state)
    , errorCode(NetworkError::NoError)
    , httpStatusCode(transferState->httpStatusCode)
    , userPauseMask(transferState->userPauseMask)
    , internalPauseMask(transferState->internalPauseMask)
    , appliedPauseMask(transferState->appliedPauseMask)
    , backpressureLimitBytes(transferState->backpressureLimitBytes)
    , backpressureResumeBytes(transferState->backpressureResumeBytes)
    , backpressurePeakBufferedBytes(transferState->backpressurePeakBufferedBytes)
    , backpressureActive(transferState->backpressureActive)
    , uploadSendPaused(transferState->uploadSendPaused)
    , bytesDownloaded(transferState->bytesDownloaded)
    , bytesUploaded(transferState->bytesUploaded)
    , downloadTotal(transferState->downloadTotal)
    , uploadTotal(transferState->uploadTotal)
    , attemptCount(0)
    , logger(transferState->logger)
    , debugTraceEnabled(transferState->debugTraceEnabled)
    , cookieFilePath(transferState->cookieFilePath)
    , cookieMode(transferState->cookieMode)
    , hstsCachePathBytes(transferState->hstsCachePathBytes)
    , altSvcCachePathBytes(transferState->altSvcCachePathBytes)
    , proxyHostBytes(transferState->proxyHostBytes)
    , proxyUserBytes(transferState->proxyUserBytes)
    , proxyPasswordBytes(transferState->proxyPasswordBytes)
    , proxyEnvironmentDisabled(transferState->proxyEnvironmentDisabled)
    , httpAuthUserBytes(transferState->httpAuthUserBytes)
    , httpAuthPasswordBytes(transferState->httpAuthPasswordBytes)
    , refererBytes(transferState->refererBytes)
    , acceptEncodingBytes(transferState->acceptEncodingBytes)
    , interfaceBytes(transferState->interfaceBytes)
    , dnsServersBytes(transferState->dnsServersBytes)
    , dohUrlBytes(transferState->dohUrlBytes)
    , resolveSlist(transferState->resolveSlist)
    , connectToSlist(transferState->connectToSlist)
    , allowedProtocolsBytes(transferState->allowedProtocolsBytes)
    , allowedRedirectProtocolsBytes(transferState->allowedRedirectProtocolsBytes)
    , sslCaCertPathBytes(transferState->sslCaCertPathBytes)
    , sslClientCertPathBytes(transferState->sslClientCertPathBytes)
    , sslClientKeyPathBytes(transferState->sslClientKeyPathBytes)
    , sslClientKeyPasswordBytes(transferState->sslClientKeyPasswordBytes)
    , sslPinnedPublicKeyBytes(transferState->sslPinnedPublicKeyBytes)
    , sslCipherListBytes(transferState->sslCipherListBytes)
    , sslTls13CiphersBytes(transferState->sslTls13CiphersBytes)
    , proxySslCaCertPathBytes(transferState->proxySslCaCertPathBytes)
    , proxySslCipherListBytes(transferState->proxySslCipherListBytes)
    , proxySslTls13CiphersBytes(transferState->proxySslTls13CiphersBytes)
    , requestBodySource(transferState->requestBodySource)
    , q_ptr(q)
{
    Q_UNUSED(body);
    curlPlan = Internal::compileRequest(Internal::normalizeRequest(req, method, requestBodySource));

    const qint64 limitBytes = request.backpressureLimitBytes();
    if (limitBytes > 0) {
        backpressureLimitBytes   = limitBytes;
        const qint64 resumeBytes = request.backpressureResumeBytes();
        if (resumeBytes > 0 && resumeBytes < limitBytes) {
            backpressureResumeBytes = resumeBytes;
        } else {
            backpressureResumeBytes = limitBytes / 2;
        }
    }
}

QCNetworkReplyPrivate::~QCNetworkReplyPrivate()
{
    finishPoolRequest();
    // 如果正在运行，从多句柄管理器移除。
    // 注意：cancel() 会在 ~QCNetworkReply() 中被调用，所以这里通常不需要额外处理。
    // 但为安全起见，如果对象直接销毁且状态仍为 Running，确保清理。
    if (multiTransferRecord && !transferRemovalRequested && q_ptr) {
        if (QThread::currentThread() == q_ptr->thread()) {
            QCCurlMultiManager::instance()->removeTransferRecord(multiTransferRecord);
        } else {
            qWarning() << "QCNetworkReplyPrivate: reply 在非所属线程销毁，无法安全从 multi engine "
                          "移除（请使用 deleteLater 或在 reply 线程销毁）";
        }
    }

    multiTransferRecord = nullptr;
}

void QCNetworkReplyPrivate::finishPoolRequest()
{
    if (poolRequestActive) {
        poolRequestActive = false;
        Internal::QCNetworkConnectionPoolManagerInternal::recordRequestCompleted(
            state == ReplyState::Finished ? curlManager.handle() : nullptr);
    }
}

CURL *QCNetworkReplyPrivate::activeCurlHandle() const noexcept
{
    return multiTransferRecord ? multiTransferRecord->handle() : curlManager.handle();
}

// ==================
// QCNetworkReply 公共接口实现
// ==================

QCNetworkLoggerHandle QCNetworkReply::loggerSnapshot() const
{
    Q_D(const QCNetworkReply);
    return d->logger;
}

QCNetworkReply::QCNetworkReply(FactoryKey,
                               const QCNetworkRequest &request,
                               HttpMethod method,
                               const Internal::RequestBody &requestBodySource,
                               const QByteArray &requestBody,
                               QObject *parent)
    : QObject(parent)
    , d_ptr(new QCNetworkReplyPrivate(this, request, method, requestBodySource, requestBody))
{
    Q_D(QCNetworkReply);

#ifdef QCURL_ENABLE_TEST_HOOKS
    setProperty(Internal::kTestCurlPlanDigestProperty,
                Internal::buildCurlPlanDigestForTest(d->curlPlan));
#endif

    // 配置 curl 选项
    if (!d->configureCurlOptions()) {
        if (d->errorCode == NetworkError::NoError) {
            d->setError(NetworkError::InvalidRequest,
                        QStringLiteral("Failed to configure curl options"));
        }
        // 工厂先返回对象；启动队列统一投递配置失败，调用方可以随后连接终态信号。
    }
}

#ifdef QCURL_ENABLE_TEST_HOOKS
QCNetworkReply::QCNetworkReply(TestOnlyKey,
                               const QCNetworkRequest &request,
                               HttpMethod method,
                               const Internal::RequestBody &requestBodySource,
                               const QByteArray &requestBody,
                               QObject *parent)
    : QObject(parent)
    , d_ptr(new QCNetworkReplyPrivate(this, request, method, requestBodySource, requestBody))
{
    Q_D(QCNetworkReply);

    setProperty(Internal::kTestCurlPlanDigestProperty,
                Internal::buildCurlPlanDigestForTest(d->curlPlan));

    // 配置 curl 选项
    if (!d->configureCurlOptions()) {
        if (d->errorCode == NetworkError::NoError) {
            d->setError(NetworkError::InvalidRequest,
                        QStringLiteral("Failed to configure curl options"));
        }
        Q_UNUSED(d->setState(ReplyState::Error));
    }
}

QCNetworkReply::QCNetworkReply(TestOnlyKey,
                               const QCNetworkRequest &request,
                               HttpMethod method,
                               const QByteArray &requestBody,
                               QObject *parent)
    : QCNetworkReply(TestOnlyKey{},
                     request,
                     method,
                     requestBody.isEmpty() ? Internal::makeEmptyRequestBody()
                                           : Internal::makeInlineRequestBody(requestBody),
                     requestBody,
                     parent)
{}
#endif

QCNetworkReply::~QCNetworkReply()
{
    Q_D(QCNetworkReply);

    // 如果正在运行，先取消
    if (d->state == ReplyState::Running || d->state == ReplyState::Paused) {
        setProperty("_qcurl_reply_destroying", true);
        cancel();
    }
}

} // namespace QCurl
