#include "QCNetworkReply.h"

#include "QCCurlMultiManager.h"
#include "QCNetworkConnectionPoolManager_p.h"
#include "QCNetworkReply_p.h"
#include "private/QCNetworkReplyProperties_p.h"
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
    , transferState(QSharedPointer<QCNetworkReplyTransferState>::create(
          Internal::compileRequest(Internal::normalizeRequest(req, method, requestBodySource))))
    , logger(transferState->logger)
    , debugTraceEnabled(transferState->debugTraceEnabled)
    , q_ptr(q)
{
    Q_UNUSED(body);
}

QCNetworkReplyPrivate::~QCNetworkReplyPrivate()
{
    transferState->trace.stop();
    if (transferToken != 0 && !transferRemovalRequested && q_ptr) {
        if (QThread::currentThread() == q_ptr->thread()) {
            transferRemovalRequested = true;
            QCCurlMultiManager::instance()->removeTransfer(transferToken);
        } else {
            qWarning() << "QCNetworkReplyPrivate: reply 在非所属线程销毁，无法安全从 multi engine "
                          "移除（请使用 deleteLater 或在 reply 线程销毁）";
        }
    }
    finishPoolRequest();
}

void QCNetworkReplyPrivate::finishPoolRequest()
{
    if (poolRequestActive) {
        poolRequestActive = false;
        Internal::QCNetworkConnectionPoolManagerInternal::recordRequestCompleted(
            transferState->lifecycle.state() == ReplyState::Finished ? curlManager.handle()
                                                                     : nullptr);
    }
}

CURL *QCNetworkReplyPrivate::activeCurlHandle() const noexcept
{
    Q_ASSERT(q_ptr && QThread::currentThread() == q_ptr->thread());
    return transferToken != 0 ? transferState->activeEasyHandle : curlManager.handle();
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
                Internal::buildCurlPlanDigestForTest(d->transferState->configuration.plan()));
#endif

    // 配置 curl 选项
    if (!d->configureCurlOptions()) {
        if (d->transferState->lifecycle.error() == NetworkError::NoError) {
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
                Internal::buildCurlPlanDigestForTest(d->transferState->configuration.plan()));

    // 配置 curl 选项
    if (!d->configureCurlOptions()) {
        if (d->transferState->lifecycle.error() == NetworkError::NoError) {
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
    if (d->transferState->lifecycle.state() == ReplyState::Running
        || d->transferState->lifecycle.state() == ReplyState::Paused) {
        setProperty(Internal::replyproperties::kDestroying, true);
        cancel();
    }
}

} // namespace QCurl
