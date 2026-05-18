#include "QCNetworkReply.h"

#include "QCCurlMultiManager.h"
#include "QCNetworkReply_p.h"
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
                                             ExecutionMode mode,
                                             const Internal::RequestBody &requestBodySource,
                                             const QByteArray &body)
    : q_ptr(q)
    , request(req)
    , httpMethod(method)
    , executionMode(mode)
    , requestBody(body)
    , normalizedRequest(Internal::normalizeRequest(req, method, mode, requestBodySource))
    , curlPlan(Internal::compileRequest(normalizedRequest))
    , multiProcessor(nullptr)
    , state(ReplyState::Idle)
    , errorCode(NetworkError::NoError)
    , bytesDownloaded(0)
    , bytesUploaded(0)
    , downloadTotal(-1)
    , uploadTotal(-1)
    , attemptCount(0)
    , cookieMode(0)
{
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
    // 如果正在运行（异步模式），从多句柄管理器移除
    // 注意：cancel() 会在 ~QCNetworkReply() 中被调用，所以这里通常不需要额外处理
    // 但为安全起见，如果对象直接销毁且状态仍为 Running，确保清理
    if (executionMode == ExecutionMode::Async
        && (state == ReplyState::Running || state == ReplyState::Paused) && q_ptr) {
        if (QThread::currentThread() == q_ptr->thread()) {
            QCCurlMultiManager::instance()->removeReply(q_ptr);
        } else {
            qWarning() << "QCNetworkReplyPrivate: reply 在非所属线程销毁，无法安全从 multi engine "
                          "移除（请使用 deleteLater 或在 reply 线程销毁）";
        }
    }

    if (resolveSlist) {
        curl_slist_free_all(resolveSlist);
        resolveSlist = nullptr;
    }

    if (connectToSlist) {
        curl_slist_free_all(connectToSlist);
        connectToSlist = nullptr;
    }
}


// ==================
// QCNetworkReply 公共接口实现
// ==================

QCNetworkReply::QCNetworkReply(FactoryKey,
                               const QCNetworkRequest &request,
                               HttpMethod method,
                               ExecutionMode mode,
                               const Internal::RequestBody &requestBodySource,
                               const QByteArray &requestBody,
                               QObject *parent)
    : QObject(parent)
    , d_ptr(new QCNetworkReplyPrivate(this,
                                      request,
                                      method,
                                      mode,
                                      requestBodySource,
                                      requestBody))
{
    Q_D(QCNetworkReply);

#ifdef QCURL_ENABLE_TEST_HOOKS
    setProperty(Internal::kTestCurlPlanDigestProperty, Internal::buildCurlPlanDigestForTest(d->curlPlan));
#endif

    // 配置 curl 选项
    if (!d->configureCurlOptions()) {
        if (d->errorCode == NetworkError::NoError) {
            d->setError(NetworkError::InvalidRequest,
                        QStringLiteral("Failed to configure curl options"));
        }
        d->setState(ReplyState::Error);
    }
}

#ifdef QCURL_ENABLE_TEST_HOOKS
QCNetworkReply::QCNetworkReply(TestOnlyKey,
                               const QCNetworkRequest &request,
                               HttpMethod method,
                               ExecutionMode mode,
                               const Internal::RequestBody &requestBodySource,
                               const QByteArray &requestBody,
                               QObject *parent)
    : QObject(parent)
    , d_ptr(new QCNetworkReplyPrivate(this,
                                      request,
                                      method,
                                      mode,
                                      requestBodySource,
                                      requestBody))
{
    Q_D(QCNetworkReply);

    setProperty(Internal::kTestCurlPlanDigestProperty, Internal::buildCurlPlanDigestForTest(d->curlPlan));

    // 配置 curl 选项
    if (!d->configureCurlOptions()) {
        if (d->errorCode == NetworkError::NoError) {
            d->setError(NetworkError::InvalidRequest,
                        QStringLiteral("Failed to configure curl options"));
        }
        d->setState(ReplyState::Error);
    }
}

QCNetworkReply::QCNetworkReply(TestOnlyKey,
                               const QCNetworkRequest &request,
                               HttpMethod method,
                               ExecutionMode mode,
                               const QByteArray &requestBody,
                               QObject *parent)
    : QCNetworkReply(TestOnlyKey{},
                     request,
                     method,
                     mode,
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
