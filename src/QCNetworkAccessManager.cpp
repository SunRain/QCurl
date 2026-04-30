#include "QCNetworkAccessManager.h"

#include "private/CurlGlobalConstructor_p.h"
#include "QCCurlMultiManager.h"
#include "QCMultipartFormData.h"
#include "QCNetworkAccessManager_p.h"
#include "QCNetworkCache.h"
#include "QCNetworkDiskCache.h"
#include "QCNetworkLogger.h"
#include "QCNetworkMiddleware.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "QCNetworkRequestScheduler.h"
#include "QCUtility.h"
#include "private/QCRequestPipeline_p.h"
#include "private/QCSingleFileMultipartBodyDevice.h"

#include <QAbstractEventDispatcher>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <QSet>
#include <QSharedPointer>
#include <QSocketNotifier>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <curl/multi.h>

#include <functional>

namespace {

static bool hasEventDispatcher(QThread *thread)
{
    return (thread != nullptr) && (QAbstractEventDispatcher::instance(thread) != nullptr);
}

static QString rawBodyOwnerThreadErrorMessage(const char *apiName)
{
    return QStringLiteral("%1: manager-level raw-body QIODevice overload 必须在 owner 线程调用")
        .arg(QString::fromUtf8(apiName));
}

static std::optional<qint64> parseContentRangeCompleteSize(const QByteArray &headerValue)
{
    const QByteArray trimmed = headerValue.trimmed();
    const QByteArray prefix = QByteArrayLiteral("bytes */");
    if (!trimmed.startsWith(prefix)) {
        return std::nullopt;
    }

    bool ok = false;
    const qint64 totalSize = trimmed.mid(prefix.size()).toLongLong(&ok);
    if (!ok || totalSize < 0) {
        return std::nullopt;
    }
    return totalSize;
}

struct ContentRangeInfo
{
    qint64 start = -1;
    qint64 end = -1;
    qint64 total = -1;
};

static std::optional<ContentRangeInfo> parseContentRangeBytesSpec(const QByteArray &headerValue)
{
    const QByteArray trimmed = headerValue.trimmed();
    const QByteArray prefix = QByteArrayLiteral("bytes ");
    if (!trimmed.startsWith(prefix)) {
        return std::nullopt;
    }

    const int dashPos = trimmed.indexOf('-', prefix.size());
    const int slashPos = trimmed.indexOf('/', prefix.size());
    if (dashPos < 0 || slashPos < 0 || dashPos >= slashPos) {
        return std::nullopt;
    }

    bool startOk = false;
    bool endOk = false;
    bool totalOk = false;
    const qint64 start = trimmed.mid(prefix.size(), dashPos - prefix.size()).toLongLong(&startOk);
    const qint64 end = trimmed.mid(dashPos + 1, slashPos - dashPos - 1).toLongLong(&endOk);
    const qint64 total = trimmed.mid(slashPos + 1).toLongLong(&totalOk);
    if (!startOk || !endOk || !totalOk || start < 0 || end < start || total < 0) {
        return std::nullopt;
    }

    return ContentRangeInfo{start, end, total};
}

} // namespace

namespace QCurl {

namespace {

constexpr const char kMiddlewareResponseInvokedProperty[] = "_qcurl_middleware_response_invoked";

QList<QCNetworkMiddleware *> sanitizeMiddlewares(const QList<QCNetworkMiddleware *> &middlewares)
{
    QList<QCNetworkMiddleware *> alive;
    alive.reserve(middlewares.size());
    for (auto *middleware : middlewares) {
        if (middleware) {
            alive.append(middleware);
        }
    }
    return alive;
}

QCNetworkMiddleware *middlewareFromEntry(const QCNetworkAccessManagerPrivate::MiddlewareEntry &entry)
{
    return entry.middleware;
}

bool middlewareEntryMatches(const QCNetworkAccessManagerPrivate::MiddlewareEntry &entry,
                            QCNetworkMiddleware *middleware)
{
    return entry.middleware == middleware;
}

QCNetworkRequest applyRequestPreSendMiddlewares(const QCNetworkRequest &request,
                                                const QList<QCNetworkMiddleware *> &middlewares)
{
    if (middlewares.isEmpty()) {
        return request;
    }

    QCNetworkRequest modifiedRequest = request;
    // 先拷贝 request，再按注册顺序串行改写，避免中间件直接污染调用方对象。
    for (auto *middleware : middlewares) {
        if (middleware) {
            middleware->onRequestPreSend(modifiedRequest);
        }
    }
    return modifiedRequest;
}

void runReplyCreatedMiddlewares(QCNetworkReply *reply,
                                const QList<QCNetworkMiddleware *> &middlewares)
{
    if (!reply || middlewares.isEmpty()) {
        return;
    }

    for (auto *middleware : middlewares) {
        if (middleware) {
            middleware->onReplyCreated(reply);
        }
    }
}

void runResponseMiddlewaresOnce(QCNetworkReply *reply,
                                QCNetworkAccessManager *manager,
                                const QList<QCNetworkMiddleware *> &middlewares)
{
    if (!reply || !manager || middlewares.isEmpty()) {
        return;
    }

    if (reply->property(kMiddlewareResponseInvokedProperty).toBool()) {
        return;
    }
    // finished 可能被同步路径或已完成 reply 触发多次探测，这里用属性做幂等保护。
    reply->setProperty(kMiddlewareResponseInvokedProperty, true);

    const QList<QCNetworkMiddleware *> activeMiddlewares = manager->middlewares();
    for (auto *middleware : middlewares) {
        if (!activeMiddlewares.contains(middleware)) {
            continue;
        }
        if (middleware) {
            middleware->onResponseReceived(reply);
        }
    }
}

void wireResponseMiddlewares(QCNetworkAccessManager *manager,
                             QCNetworkReply *reply,
                             const QList<QCNetworkMiddleware *> &middlewares)
{
    if (!manager || !reply || middlewares.isEmpty()) {
        return;
    }

    QPointer<QCNetworkReply> safeReply(reply);
    QPointer<QCNetworkAccessManager> safeManager(manager);
    QObject::connect(reply, &QCNetworkReply::finished, manager, [safeReply, safeManager, middlewares]() {
        if (!safeManager) {
            return;
        }
        if (!safeReply) {
            return;
        }
        runResponseMiddlewaresOnce(safeReply.data(), safeManager.data(), middlewares);
    });

    // 对已完成 reply 立即补跑一次，避免“先 finished、后接线”导致漏掉响应中间件。
    if (reply->isFinished()) {
        runResponseMiddlewaresOnce(reply, manager, middlewares);
    }
}

struct ResumableWriteContext
{
    bool modeDecided = false;
    bool appendMode = false;
    bool safeOverwriteMode = false;
    bool writeFailed = false;
    QSharedPointer<QFile> appendFile;
    QSharedPointer<QFile> directFile;
    QSharedPointer<QSaveFile> overwriteFile;
};

bool isResumableAlreadyComplete(QCNetworkReply *reply, qint64 existingSize)
{
    return reply && existingSize > 0 && reply->httpStatusCode() == 416
           && parseContentRangeCompleteSize(reply->rawHeader(QByteArrayLiteral("Content-Range")))
                  .value_or(-1)
                  == existingSize;
}

void closeResumableTargets(const QSharedPointer<ResumableWriteContext> &context)
{
    if (context->appendFile && context->appendFile->isOpen()) {
        context->appendFile->close();
    }
    if (context->directFile && context->directFile->isOpen()) {
        context->directFile->close();
    }
    if (context->overwriteFile && context->overwriteFile->isOpen()) {
        context->overwriteFile->cancelWriting();
    }
}

QIODevice *activeResumableTarget(const QSharedPointer<ResumableWriteContext> &context)
{
    if (context->appendMode) {
        return context->appendFile.data();
    }
    if (context->safeOverwriteMode) {
        return context->overwriteFile.data();
    }
    return context->directFile.data();
}

std::optional<QString> decideResumableWriteMode(QCNetworkReply *reply,
                                                const QSharedPointer<ResumableWriteContext> &context,
                                                const QString &savePath,
                                                qint64 existingSize,
                                                bool hadExistingFile)
{
    if (isResumableAlreadyComplete(reply, existingSize)) {
        context->modeDecided = true;
        context->appendMode = false;
        context->safeOverwriteMode = false;
        return std::nullopt;
    }

    const auto contentRange =
        parseContentRangeBytesSpec(reply->rawHeader(QByteArrayLiteral("Content-Range")));
    context->appendMode = existingSize > 0 && reply->httpStatusCode() == 206
                          && contentRange.has_value() && contentRange->start == existingSize;
    if (reply->httpStatusCode() == 206 && !context->appendMode) {
        return QStringLiteral(
                   "downloadFileResumable: 206 响应的 Content-Range.start 与本地文件大小不匹配: %1")
            .arg(savePath);
    }
    context->safeOverwriteMode = !context->appendMode && hadExistingFile;
    context->modeDecided = true;
    return std::nullopt;
}

std::optional<QString> ensureResumableWriteTarget(
    QCNetworkReply *reply,
    const QSharedPointer<ResumableWriteContext> &context,
    const QString &savePath,
    qint64 existingSize,
    bool hadExistingFile)
{
    if (!context->modeDecided) {
        if (const auto error =
                decideResumableWriteMode(reply, context, savePath, existingSize, hadExistingFile);
            error.has_value()) {
            return error;
        }
    }
    if (isResumableAlreadyComplete(reply, existingSize)) {
        return std::nullopt;
    }
    if (context->appendMode && !context->appendFile->isOpen()
        && !context->appendFile->open(QIODevice::Append)) {
        return QStringLiteral("downloadFileResumable: 无法以追加模式打开目标文件: %1")
            .arg(context->appendFile->fileName());
    }
    if (context->safeOverwriteMode) {
        if (!context->overwriteFile) {
            context->overwriteFile = QSharedPointer<QSaveFile>::create(savePath);
        }
        if (!context->overwriteFile->isOpen()
            && !context->overwriteFile->open(QIODevice::WriteOnly)) {
            return QStringLiteral("downloadFileResumable: 无法以安全覆盖模式打开目标文件: %1")
                .arg(savePath);
        }
    } else if (!context->appendMode && !context->directFile->isOpen()
               && !context->directFile->open(QIODevice::WriteOnly)) {
        return QStringLiteral("downloadFileResumable: 无法创建目标文件: %1").arg(savePath);
    }
    return std::nullopt;
}

std::optional<QString> commitResumableOverwrite(
    QCNetworkReply *reply,
    const QSharedPointer<ResumableWriteContext> &context,
    const QString &savePath,
    qint64 existingSize,
    bool hadExistingFile)
{
    if (isResumableAlreadyComplete(reply, existingSize)) {
        return std::nullopt;
    }
    if (const auto error =
            ensureResumableWriteTarget(reply, context, savePath, existingSize, hadExistingFile);
        error.has_value()) {
        context->writeFailed = true;
        return error;
    }
    if (!context->safeOverwriteMode || !context->overwriteFile
        || !context->overwriteFile->isOpen()) {
        return std::nullopt;
    }
    if (context->overwriteFile->commit()) {
        return std::nullopt;
    }
    context->writeFailed = true;
    return QStringLiteral("downloadFileResumable: 覆盖写入提交失败: %1").arg(savePath);
}

void writeResumableChunk(QCNetworkReply *reply,
                         const QSharedPointer<ResumableWriteContext> &context,
                         const QString &savePath,
                         qint64 existingSize,
                         bool hadExistingFile)
{
    if (const auto error =
            ensureResumableWriteTarget(reply, context, savePath, existingSize, hadExistingFile);
        error.has_value()) {
        context->writeFailed = true;
        closeResumableTargets(context);
        reply->abortWithError(NetworkError::InvalidRequest, error.value());
        return;
    }

    QIODevice *target = activeResumableTarget(context);
    if (!target || !target->isWritable()) {
        qWarning() << "Resumable target is not writable:" << savePath;
        context->writeFailed = true;
        closeResumableTargets(context);
        reply->cancel();
        return;
    }

    auto data = reply->readAll();
    if (!data.has_value() || data->isEmpty()) {
        return;
    }

    const QByteArray &chunk = data.value();
    if (target->write(chunk) != chunk.size()) {
        context->writeFailed = true;
        closeResumableTargets(context);
        reply->abortWithError(
            NetworkError::InvalidRequest,
            QStringLiteral("downloadFileResumable: 写入目标文件失败: %1").arg(savePath));
    }
}

void finishResumableDownload(QCNetworkReply *reply,
                             const QSharedPointer<ResumableWriteContext> &context,
                             const QString &savePath,
                             qint64 existingSize)
{
    const bool alreadyComplete =
        reply->error() == NetworkError::NoError && isResumableAlreadyComplete(reply, existingSize);

    if (reply->error() != NetworkError::NoError || !context->safeOverwriteMode
        || !context->overwriteFile || !context->overwriteFile->isOpen()) {
        closeResumableTargets(context);
    }
    if (context->appendFile && context->appendFile->isOpen()) {
        context->appendFile->close();
    }
    if (context->directFile && context->directFile->isOpen()) {
        context->directFile->close();
    }
    if (context->writeFailed) {
        qWarning() << "Download failed while writing file:" << savePath << reply->errorString();
    } else if (alreadyComplete) {
        qDebug() << "Download already complete:" << savePath;
    } else if (reply->error() == NetworkError::NoError) {
        qDebug() << "Download completed:" << savePath;
    } else {
        qWarning() << "Download failed:" << reply->errorString();
    }
}

void wireResumableDownload(QCNetworkReply *reply,
                           QCNetworkReplyPrivate *replyPrivate,
                           const QString &savePath,
                           qint64 existingSize,
                           bool hadExistingFile)
{
    if (!reply || !replyPrivate) {
        return;
    }

    reply->setProperty("_qcurl_resumable_existing_size", existingSize);

    auto writeContext = QSharedPointer<ResumableWriteContext>::create();
    writeContext->appendFile = QSharedPointer<QFile>::create(savePath);
    writeContext->directFile = QSharedPointer<QFile>::create(savePath);

    replyPrivate->beforeFinishTransition =
        [reply, writeContext, savePath, existingSize, hadExistingFile]() {
        return commitResumableOverwrite(reply,
                                        writeContext,
                                        savePath,
                                        existingSize,
                                        hadExistingFile);
    };

    QObject::connect(reply,
                     &QCNetworkReply::readyRead,
                     reply,
                     [reply, writeContext, savePath, existingSize, hadExistingFile]() {
        writeResumableChunk(reply, writeContext, savePath, existingSize, hadExistingFile);
    });

    QObject::connect(reply,
                     &QCNetworkReply::finished,
                     reply,
                     [reply, writeContext, savePath, existingSize]() {
        finishResumableDownload(reply, writeContext, savePath, existingSize);
    });
}

} // namespace

QCNetworkAccessManager::QCNetworkAccessManager(QObject *parent)
    : QObject(parent)
    , d_ptr(new QCNetworkAccessManagerPrivate(this))
{
    CurlGlobalConstructor::instance();
}

QCNetworkAccessManager::~QCNetworkAccessManager()
{
    clearMiddlewares();
}

QCNetworkReply *QCNetworkAccessManager::createReply(const QCNetworkRequest &request,
                                                    HttpMethod method,
                                                    bool async,
                                                    const Internal::RequestBody &requestBodySource,
                                                    const QByteArray &body,
                                                    QObject *parent)
{
    const auto mode = async ? ExecutionMode::Async : ExecutionMode::Sync;
    auto *reply = new QCNetworkReply(QCNetworkReply::FactoryKey{},
                                     request,
                                     method,
                                     mode,
                                     requestBodySource,
                                     body,
                                     parent);
    applyReplyDefaults(reply);
    return reply;
}

QCNetworkReply *QCNetworkAccessManager::createManagedReply(
    const QCNetworkRequest &request,
    HttpMethod method,
    bool async,
    const Internal::RequestBody &requestBodySource,
    const QByteArray &body,
    const QList<QCNetworkMiddleware *> &middlewares)
{
    auto *reply = createReply(request, method, async, requestBodySource, body, this);
    prepareManagedReply(reply, middlewares);

    if (!reply) {
        return nullptr;
    }

    startManagedReply(reply, request, async);
    return reply;
}

QCNetworkReply *QCNetworkAccessManager::createNoEventLoopErrorReply(
    const QCNetworkRequest &request,
    HttpMethod method,
    const Internal::RequestBody &requestBodySource,
    const QByteArray &body,
    QObject *parent,
    const char *apiName)
{
    auto *reply = createReply(request, method, true, requestBodySource, body, parent);
    if (!reply) {
        return nullptr;
    }

    reply->abortWithError(NetworkError::InvalidRequest,
                          QStringLiteral("%1: owner 线程缺少 Qt 事件循环，无法执行异步请求")
                              .arg(QString::fromUtf8(apiName)));
    return reply;
}

QCNetworkReply *QCNetworkAccessManager::createInvalidRequestReply(const QCNetworkRequest &request,
                                                                  HttpMethod method,
                                                                  bool async,
                                                                  const QString &message,
                                                                  QObject *parent)
{
    auto *reply =
        createReply(request, method, async, Internal::makeEmptyRequestBody(), QByteArray(), parent);
    if (!reply) {
        return nullptr;
    }

    reply->abortWithError(NetworkError::InvalidRequest, message);
    return reply;
}

QCNetworkReply *QCNetworkAccessManager::createManagedErrorReply(
    const QCNetworkRequest &request,
    HttpMethod method,
    const QString &message,
    const QList<QCNetworkMiddleware *> &middlewares)
{
    auto *reply = createReply(request,
                              method,
                              true,
                              Internal::makeEmptyRequestBody(),
                              QByteArray(),
                              this);
    prepareManagedReply(reply, middlewares);
    QMetaObject::invokeMethod(
        reply,
        [reply, message]() { reply->abortWithError(NetworkError::InvalidRequest, message); },
        Qt::QueuedConnection);
    return reply;
}

void QCNetworkAccessManager::applyReplyDefaults(QCNetworkReply *reply) const
{
    Q_D(const QCNetworkAccessManager);
    if (!reply) {
        return;
    }

    if (d->cookieModeFlag != NotOpen && !d->cookieFilePath.isEmpty()) {
        reply->d_func()->cookieFilePath = d->cookieFilePath;
        reply->d_func()->cookieMode     = d->cookieModeFlag;
    }
}

void QCNetworkAccessManager::prepareManagedReply(
    QCNetworkReply *reply,
    const QList<QCNetworkMiddleware *> &middlewares) const
{
    wireResponseMiddlewares(const_cast<QCNetworkAccessManager *>(this), reply, middlewares);
    runReplyCreatedMiddlewares(reply, middlewares);
}

void QCNetworkAccessManager::startManagedReply(QCNetworkReply *reply,
                                               const QCNetworkRequest &request,
                                               bool async) const
{
    if (!reply) {
        return;
    }

    if (async && isSchedulerEnabled()) {
        // lane/priority 会在 scheduler 入队时快照，后续信号与 lane 级取消都以该快照为准。
        scheduler()->scheduleReply(reply, request.lane(), request.priority());
        return;
    }

    reply->execute();
}

QCNetworkReply *QCNetworkAccessManager::dispatchManagedSendRequest(
    const QCNetworkRequest &request,
    HttpMethod method,
    bool async,
    const Internal::RequestBody &requestBodySource,
    const QByteArray &body,
    const char *apiName)
{
    return dispatchSendRequest(
        request,
        method,
        async,
        requestBodySource,
        body,
        apiName,
        [this, request, method, async, requestBodySource, body]() {
            const auto middlewaresSnapshot = middlewares();
            const QCNetworkRequest modifiedRequest = applyRequestPreSendMiddlewares(
                request, middlewaresSnapshot);
            return createManagedReply(modifiedRequest,
                                      method,
                                      async,
                                      requestBodySource,
                                      body,
                                      middlewaresSnapshot);
        });
}

QCNetworkReply *QCNetworkAccessManager::dispatchSendRequest(const QCNetworkRequest &request,
                                                            HttpMethod method,
                                                            bool async,
                                                            const Internal::RequestBody &requestBodySource,
                                                            const QByteArray &body,
                                                            const char *apiName,
                                                            const ReplyFactory &impl)
{
    if (QThread::currentThread() != thread()) {
        if (!hasEventDispatcher(thread())) {
            return createNoEventLoopErrorReply(
                request, method, requestBodySource, body, nullptr, apiName);
        }

        QCNetworkReply *result = nullptr;
        QElapsedTimer timer;
        timer.start();
        QMetaObject::invokeMethod(
            this, [impl, &result]() { result = impl(); }, Qt::BlockingQueuedConnection);
        if (timer.elapsed() > 1000) {
            qWarning() << apiName << ": cross-thread blocking call took" << timer.elapsed()
                       << "ms (potential deadlock risk if owner thread is blocked)";
        }
        return result;
    }

    if (async && !hasEventDispatcher(thread())) {
        return createNoEventLoopErrorReply(request, method, requestBodySource, body, this, apiName);
    }

    return impl();
}

QString QCNetworkAccessManager::cookieFilePath() const
{
    Q_D(const QCNetworkAccessManager);
    return d->cookieFilePath;
}

QCNetworkAccessManager::CookieFileModeFlag QCNetworkAccessManager::cookieFileMode() const
{
    Q_D(const QCNetworkAccessManager);
    return d->cookieModeFlag;
}

void QCNetworkAccessManager::setCookieFilePath(const QString &cookieFilePath,
                                               CookieFileModeFlag flag)
{
    Q_D(QCNetworkAccessManager);
    d->cookieFilePath = cookieFilePath;
    d->cookieModeFlag = flag;
}

bool QCNetworkAccessManager::importCookies(const QList<QNetworkCookie> &cookies,
                                           const QUrl &originUrl,
                                           QString *error)
{
    if (QThread::currentThread() != thread()) {
        if (!hasEventDispatcher(thread())) {
            if (error) {
                *error = QStringLiteral("QCNetworkAccessManager::importCookies: manager "
                                        "所在线程无事件循环，无法跨线程提交");
            }
            return false;
        }

        bool ok = false;
        QString localError;
        QMetaObject::invokeMethod(
            this,
            [this, cookies, originUrl, &ok, &localError]() {
                ok = importCookies(cookies, originUrl, &localError);
            },
            Qt::BlockingQueuedConnection);

        if (error) {
            *error = localError;
        }
        return ok;
    }

    auto *multi = QCCurlMultiManager::instance();
    if (!multi) {
        if (error) {
            *error = QStringLiteral("QCCurlMultiManager 不可用");
        }
        return false;
    }
    return multi->importCookiesForManager(this, cookies, originUrl, error);
}

QList<QNetworkCookie> QCNetworkAccessManager::exportCookies(const QUrl &filterUrl,
                                                            QString *error) const
{
    if (QThread::currentThread() != thread()) {
        if (!hasEventDispatcher(thread())) {
            if (error) {
                *error = QStringLiteral("QCNetworkAccessManager::exportCookies: manager "
                                        "所在线程无事件循环，无法跨线程提交");
            }
            return {};
        }

        QList<QNetworkCookie> cookies;
        QString localError;
        auto *mutableThis = const_cast<QCNetworkAccessManager *>(this);
        QMetaObject::invokeMethod(
            mutableThis,
            [this, filterUrl, &cookies, &localError]() {
                cookies = exportCookies(filterUrl, &localError);
            },
            Qt::BlockingQueuedConnection);

        if (error) {
            *error = localError;
        }
        return cookies;
    }

    auto *multi = QCCurlMultiManager::instance();
    if (!multi) {
        if (error) {
            *error = QStringLiteral("QCCurlMultiManager 不可用");
        }
        return {};
    }
    return multi->exportCookiesForManager(this, filterUrl, error);
}

bool QCNetworkAccessManager::clearAllCookies(QString *error)
{
    if (QThread::currentThread() != thread()) {
        if (!hasEventDispatcher(thread())) {
            if (error) {
                *error = QStringLiteral("QCNetworkAccessManager::clearAllCookies: manager "
                                        "所在线程无事件循环，无法跨线程提交");
            }
            return false;
        }

        bool ok = false;
        QString localError;
        QMetaObject::invokeMethod(
            this,
            [this, &ok, &localError]() { ok = clearAllCookies(&localError); },
            Qt::BlockingQueuedConnection);

        if (error) {
            *error = localError;
        }
        return ok;
    }

    auto *multi = QCCurlMultiManager::instance();
    if (!multi) {
        if (error) {
            *error = QStringLiteral("QCCurlMultiManager 不可用");
        }
        return false;
    }
    return multi->clearAllCookiesForManager(this, error);
}

void QCNetworkAccessManager::setShareHandleConfig(const ShareHandleConfig &config)
{
    Q_D(QCNetworkAccessManager);
    d->shareHandleConfig = config;
}

QCNetworkAccessManager::ShareHandleConfig QCNetworkAccessManager::shareHandleConfig() const noexcept
{
    Q_D(const QCNetworkAccessManager);
    return d->shareHandleConfig;
}

void QCNetworkAccessManager::setHstsAltSvcCacheConfig(const HstsAltSvcCacheConfig &config)
{
    Q_D(QCNetworkAccessManager);
    d->hstsAltSvcCacheConfig = config;
}

QCNetworkAccessManager::HstsAltSvcCacheConfig QCNetworkAccessManager::hstsAltSvcCacheConfig()
    const noexcept
{
    Q_D(const QCNetworkAccessManager);
    return d->hstsAltSvcCacheConfig;
}

// ==================
// 核心 API 实现
// ==================

QCNetworkReply *QCNetworkAccessManager::sendHead(const QCNetworkRequest &request)
{
    return dispatchManagedSendRequest(request,
                                      HttpMethod::Head,
                                      true,
                                      Internal::makeEmptyRequestBody(),
                                      QByteArray(),
                                      "QCNetworkAccessManager::sendHead");
}

QCNetworkReply *QCNetworkAccessManager::sendGet(const QCNetworkRequest &request)
{
    return dispatchManagedSendRequest(request,
                                      HttpMethod::Get,
                                      true,
                                      Internal::makeEmptyRequestBody(),
                                      QByteArray(),
                                      "QCNetworkAccessManager::sendGet");
}

QCNetworkReply *QCNetworkAccessManager::sendPost(const QCNetworkRequest &request,
                                                 const QByteArray &data)
{
    return dispatchManagedSendRequest(request,
                                      HttpMethod::Post,
                                      true,
                                      Internal::makeInlineRequestBody(data),
                                      data,
                                      "QCNetworkAccessManager::sendPost");
}

QCNetworkReply *QCNetworkAccessManager::sendPost(const QCNetworkRequest &request,
                                                 QIODevice *device,
                                                 std::optional<qint64> sizeBytes)
{
    constexpr const char *apiName = "QCNetworkAccessManager::sendPost";
    if (QThread::currentThread() != thread()) {
        return createInvalidRequestReply(request,
                                         HttpMethod::Post,
                                         true,
                                         rawBodyOwnerThreadErrorMessage(apiName),
                                         nullptr);
    }

    return dispatchManagedSendRequest(request,
                                      HttpMethod::Post,
                                      true,
                                      Internal::makeDeviceRequestBody(device, sizeBytes, true),
                                      QByteArray(),
                                      apiName);
}

QCNetworkReply *QCNetworkAccessManager::sendPut(const QCNetworkRequest &request,
                                                const QByteArray &data)
{
    return dispatchManagedSendRequest(request,
                                      HttpMethod::Put,
                                      true,
                                      Internal::makeInlineRequestBody(data),
                                      data,
                                      "QCNetworkAccessManager::sendPut");
}

QCNetworkReply *QCNetworkAccessManager::sendPut(const QCNetworkRequest &request,
                                                QIODevice *device,
                                                std::optional<qint64> sizeBytes)
{
    constexpr const char *apiName = "QCNetworkAccessManager::sendPut";
    if (QThread::currentThread() != thread()) {
        return createInvalidRequestReply(request,
                                         HttpMethod::Put,
                                         true,
                                         rawBodyOwnerThreadErrorMessage(apiName),
                                         nullptr);
    }

    return dispatchManagedSendRequest(request,
                                      HttpMethod::Put,
                                      true,
                                      Internal::makeDeviceRequestBody(device, sizeBytes, false),
                                      QByteArray(),
                                      apiName);
}

QCNetworkReply *QCNetworkAccessManager::sendDelete(const QCNetworkRequest &request)
{
    return sendDelete(request, QByteArray());
}

QCNetworkReply *QCNetworkAccessManager::sendDelete(const QCNetworkRequest &request,
                                                   const QByteArray &data)
{
    return dispatchManagedSendRequest(request,
                                      HttpMethod::Delete,
                                      true,
                                      Internal::makeInlineRequestBody(data),
                                      data,
                                      "QCNetworkAccessManager::sendDelete");
}

QCNetworkReply *QCNetworkAccessManager::sendPatch(const QCNetworkRequest &request,
                                                  const QByteArray &data)
{
    return dispatchManagedSendRequest(request,
                                      HttpMethod::Patch,
                                      true,
                                      Internal::makeInlineRequestBody(data),
                                      data,
                                      "QCNetworkAccessManager::sendPatch");
}

QCNetworkReply *QCNetworkAccessManager::sendGetSync(const QCNetworkRequest &request)
{
    return dispatchManagedSendRequest(request,
                                      HttpMethod::Get,
                                      false,
                                      Internal::makeEmptyRequestBody(),
                                      QByteArray(),
                                      "QCNetworkAccessManager::sendGetSync");
}

QCNetworkReply *QCNetworkAccessManager::sendPostSync(const QCNetworkRequest &request,
                                                     const QByteArray &data)
{
    return dispatchManagedSendRequest(request,
                                      HttpMethod::Post,
                                      false,
                                      Internal::makeInlineRequestBody(data),
                                      data,
                                      "QCNetworkAccessManager::sendPostSync");
}

QCNetworkReply *QCNetworkAccessManager::sendPostSync(const QCNetworkRequest &request,
                                                     QIODevice *device,
                                                     std::optional<qint64> sizeBytes)
{
    constexpr const char *apiName = "QCNetworkAccessManager::sendPostSync";
    if (QThread::currentThread() != thread()) {
        return createInvalidRequestReply(request,
                                         HttpMethod::Post,
                                         false,
                                         rawBodyOwnerThreadErrorMessage(apiName),
                                         nullptr);
    }

    return dispatchManagedSendRequest(request,
                                      HttpMethod::Post,
                                      false,
                                      Internal::makeDeviceRequestBody(device, sizeBytes, true),
                                      QByteArray(),
                                      apiName);
}

QCNetworkReply *QCNetworkAccessManager::sendPutSync(const QCNetworkRequest &request,
                                                    const QByteArray &data)
{
    return dispatchManagedSendRequest(request,
                                      HttpMethod::Put,
                                      false,
                                      Internal::makeInlineRequestBody(data),
                                      data,
                                      "QCNetworkAccessManager::sendPutSync");
}

QCNetworkReply *QCNetworkAccessManager::sendPutSync(const QCNetworkRequest &request,
                                                    QIODevice *device,
                                                    std::optional<qint64> sizeBytes)
{
    constexpr const char *apiName = "QCNetworkAccessManager::sendPutSync";
    if (QThread::currentThread() != thread()) {
        return createInvalidRequestReply(request,
                                         HttpMethod::Put,
                                         false,
                                         rawBodyOwnerThreadErrorMessage(apiName),
                                         nullptr);
    }

    return dispatchManagedSendRequest(request,
                                      HttpMethod::Put,
                                      false,
                                      Internal::makeDeviceRequestBody(device, sizeBytes, false),
                                      QByteArray(),
                                      apiName);
}

// ==================
// 请求优先级调度
// ==================

void QCNetworkAccessManager::enableRequestScheduler(bool enabled)
{
    Q_D(QCNetworkAccessManager);
    d->schedulerEnabled = enabled;
}

bool QCNetworkAccessManager::isSchedulerEnabled() const
{
    Q_D(const QCNetworkAccessManager);
    return d->schedulerEnabled;
}

QCNetworkRequestScheduler *QCNetworkAccessManager::scheduler() const
{
    if (QThread::currentThread() != thread()) {
        // 这里若直接返回 instance()，调用方拿到的会是“当前调用线程”的错误 scheduler。
        qWarning()
            << "QCNetworkAccessManager::scheduler: called from non-owner thread; use "
               "schedulerOnOwnerThread() to fetch the manager owner-thread scheduler";
        Q_ASSERT_X(QThread::currentThread() == thread(),
                   "QCNetworkAccessManager::scheduler",
                   "scheduler() must be called on the manager owner thread");
        return nullptr;
    }

    return QCNetworkRequestScheduler::instance();
}

QCNetworkRequestScheduler *QCNetworkAccessManager::schedulerOnOwnerThread() const
{
    // BlockingQueuedConnection 依赖 owner thread 的事件循环仍然可达。
    if (!hasEventDispatcher(thread())) {
        qWarning() << "QCNetworkAccessManager::schedulerOnOwnerThread: manager owner thread has "
                      "no Qt event dispatcher; blocking call is rejected";
        return nullptr;
    }

    if (QThread::currentThread() == thread()) {
        return QCNetworkRequestScheduler::instance();
    }

    auto *mutableThis = const_cast<QCNetworkAccessManager *>(this);
    QCNetworkRequestScheduler *result = nullptr;
    QElapsedTimer timer;
    timer.start();
    // 跨线程 accessor 必须回 owner thread 获取共享 scheduler，避免 thread-local 漂移。
    const bool invoked = QMetaObject::invokeMethod(
        mutableThis,
        [&result]() { result = QCNetworkRequestScheduler::instance(); },
        Qt::BlockingQueuedConnection);
    if (!invoked) {
        qWarning() << "QCNetworkAccessManager::schedulerOnOwnerThread: failed to marshal call "
                      "back to the manager owner thread";
        return nullptr;
    }
    // 超时只告警不改变结果，用于暴露 owner thread 被锁住的风险窗口。
    if (timer.elapsed() > 1000) {
        qWarning() << "QCNetworkAccessManager::schedulerOnOwnerThread: cross-thread blocking call "
                      "took"
                   << timer.elapsed() << "ms (potential deadlock risk if owner thread is blocked)";
    }
    return result;
}

// ==================
// JSON 快捷方法实现
// ==================

QCNetworkReply *QCNetworkAccessManager::postJson(const QUrl &url, const QJsonObject &json)
{
    // 序列化 JSON 对象
    QJsonDocument doc(json);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    // 创建请求并设置 Content-Type
    QCNetworkRequest request(url);
    request.setRawHeader("Content-Type", "application/json");

    // 发送 POST 请求
    return sendPost(request, jsonData);
}

QCNetworkReply *QCNetworkAccessManager::postForm(const QUrl &url,
                                                 const QMap<QString, QString> &formData)
{
    // URL编码表单数据
    QStringList pairs;
    for (auto it = formData.constBegin(); it != formData.constEnd(); ++it) {
        QString key   = QUrl::toPercentEncoding(it.key());
        QString value = QUrl::toPercentEncoding(it.value());
        pairs.append(QStringLiteral("%1=%2").arg(key, value));
    }
    QByteArray encodedData = pairs.join("&").toUtf8();

    // 创建请求并设置 Content-Type
    QCNetworkRequest request(url);
    request.setRawHeader("Content-Type", "application/x-www-form-urlencoded");

    // 发送 POST 请求
    return sendPost(request, encodedData);
}

// ==================
// Multipart/form-data 支持
// ==================

QCNetworkReply *QCNetworkAccessManager::postMultipart(const QUrl &url,
                                                      const QCMultipartFormData &formData)
{
    // 编码 Multipart 表单数据
    QByteArray multipartData = formData.toByteArray();

    // 创建请求并设置 Content-Type
    QCNetworkRequest request(url);
    request.setRawHeader("Content-Type", formData.contentType().toUtf8());

    // 发送 POST 请求
    return sendPost(request, multipartData);
}

QCNetworkReply *QCNetworkAccessManager::postMultipart(const QCNetworkRequest &request,
                                                      const QCMultipartFormData &formData)
{
    // 创建请求副本并设置 Content-Type
    QCNetworkRequest modifiedRequest = request;
    modifiedRequest.setRawHeader("Content-Type", formData.contentType().toUtf8());

    // 编码 Multipart 表单数据
    QByteArray multipartData = formData.toByteArray();

    // 发送 POST 请求
    return sendPost(modifiedRequest, multipartData);
}

QCNetworkReply *QCNetworkAccessManager::postMultipartFile(const QUrl &url,
                                                          const QString &fieldName,
                                                          const QString &filePath,
                                                          const QString &mimeType)
{
    QCMultipartFormData formData;
    if (!formData.addFileField(fieldName, filePath, mimeType)) {
        const auto middlewaresSnapshot = middlewares();
        const QCNetworkRequest modifiedRequest =
            applyRequestPreSendMiddlewares(QCNetworkRequest(url), middlewaresSnapshot);
        return createManagedErrorReply(
            modifiedRequest,
            HttpMethod::Post,
            QStringLiteral("postMultipartFile: 无法加入文件字段: %1").arg(filePath),
            middlewaresSnapshot);
    }

    return postMultipart(url, formData);
}

QCNetworkReply *QCNetworkAccessManager::postMultipartDevice(const QUrl &url,
                                                            const QString &fieldName,
                                                            QIODevice *device,
                                                            const QString &fileName,
                                                            const QString &mimeType,
                                                            std::optional<qint64> sizeBytes)
{
    constexpr const char *apiName = "QCNetworkAccessManager::postMultipartDevice";
    QCNetworkRequest request(url);
    if (QThread::currentThread() != thread()) {
        return createInvalidRequestReply(request,
                                         HttpMethod::Post,
                                         true,
                                         rawBodyOwnerThreadErrorMessage(apiName),
                                         nullptr);
    }

    return dispatchSendRequest(
        request,
        HttpMethod::Post,
        true,
        Internal::makeEmptyRequestBody(),
        QByteArray(),
        apiName,
        [this, request, fieldName, device, fileName, mimeType, sizeBytes]() {
            const auto middlewaresSnapshot = middlewares();
            QCNetworkRequest modifiedRequest =
                applyRequestPreSendMiddlewares(request, middlewaresSnapshot);
            auto fail = [this, &modifiedRequest, &middlewaresSnapshot](const QString &message) {
                return createManagedErrorReply(modifiedRequest,
                                               HttpMethod::Post,
                                               message,
                                               middlewaresSnapshot);
            };

            if (!device) {
                return fail(QStringLiteral("postMultipartDevice: 源 QIODevice 为空"));
            }
            if (device->thread() != thread()) {
                return fail(QStringLiteral("postMultipartDevice: 源 QIODevice 与 Reply 不在同一线程"));
            }
            if (!device->isReadable()) {
                return fail(QStringLiteral("postMultipartDevice: 源 QIODevice 不可读"));
            }
            if (device->isSequential() || !sizeBytes.has_value() || sizeBytes.value() < 0) {
                return fail(QStringLiteral(
                    "postMultipartDevice: 单文件 multipart 设备上传要求已知长度且设备可 seek"));
            }

            QCMultipartFormData formData;
            modifiedRequest.setRawHeader("Content-Type", formData.contentType().toUtf8());

            auto *multipartDevice = new Internal::QCSingleFileMultipartBodyDevice(
                formData.boundary(),
                fieldName,
                device,
                fileName,
                mimeType.isEmpty() ? QStringLiteral("application/octet-stream") : mimeType,
                sizeBytes.value(),
                this);
            auto *reply = createReply(modifiedRequest,
                                      HttpMethod::Post,
                                      true,
                                      Internal::makeDeviceRequestBody(multipartDevice,
                                                                      multipartDevice->size(),
                                                                      false,
                                                                      false),
                                      QByteArray(),
                                      this);
            if (reply) {
                multipartDevice->setParent(reply);
                prepareManagedReply(reply, middlewaresSnapshot);
                startManagedReply(reply, modifiedRequest, true);
            } else {
                multipartDevice->deleteLater();
            }
            return reply;
        });
}

// ==================
// 文件操作便捷 API
// ==================

QCNetworkReply *QCNetworkAccessManager::downloadFile(const QUrl &url, const QString &savePath)
{
    // 创建请求
    QCNetworkRequest request(url);

    // 发送 GET 请求
    QCNetworkReply *reply = sendGet(request);

    // 连接信号以保存文件
    QObject::connect(reply, &QCNetworkReply::finished, reply, [reply, savePath]() {
        if (reply->error() == NetworkError::NoError) {
            // 读取响应数据
            auto responseData = reply->readAll();
            if (responseData.has_value()) {
                // 保存到文件
                QFile file(savePath);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(responseData.value());
                    file.close();
                    qDebug() << "File saved to:" << savePath;
                } else {
                    qWarning() << "Cannot open file for writing:" << savePath;
                }
            }
        }
    });

    return reply;
}

// ==================
// 流式下载/上传 API
// ==================

QCNetworkReply *QCNetworkAccessManager::downloadToDevice(const QUrl &url, QIODevice *device)
{
    const auto middlewaresSnapshot = middlewares();

    QCNetworkRequest request(url);
    const QCNetworkRequest modifiedRequest = applyRequestPreSendMiddlewares(request,
                                                                            middlewaresSnapshot);

    auto *reply = createReply(modifiedRequest,
                              HttpMethod::Get,
                              true,
                              Internal::makeEmptyRequestBody(),
                              QByteArray(),
                              this);
    prepareManagedReply(reply, middlewaresSnapshot);

    if (!device || !device->isWritable()) {
        qWarning() << "downloadToDevice: device is null or not writable";
        QMetaObject::invokeMethod(
            reply,
            [reply]() {
                reply->abortWithError(NetworkError::InvalidRequest,
                                      QStringLiteral(
                                          "downloadToDevice: 目标 QIODevice 为空或不可写"));
            },
            Qt::QueuedConnection);
        return reply;
    }

    if (device->thread() != thread()) {
        qWarning() << "downloadToDevice: device thread mismatch. deviceThread=" << device->thread()
                   << "managerThread=" << thread();
        QMetaObject::invokeMethod(
            reply,
            [reply]() {
                reply
                    ->abortWithError(NetworkError::InvalidRequest,
                                     QStringLiteral(
                                         "downloadToDevice: 目标 QIODevice 与 Reply 不在同一线程"));
            },
            Qt::QueuedConnection);
        return reply;
    }

    QPointer<QIODevice> safeDevice(device);

    QObject::connect(device, &QObject::destroyed, reply, [reply]() {
        reply->abortWithError(NetworkError::InvalidRequest,
                              QStringLiteral("downloadToDevice: 目标 QIODevice 在传输中被销毁"));
    });

    // 连接 readyRead 信号实现流式写入
    QObject::connect(reply, &QCNetworkReply::readyRead, reply, [reply, safeDevice]() {
        if (!safeDevice) {
            reply->abortWithError(NetworkError::InvalidRequest,
                                  QStringLiteral(
                                      "downloadToDevice: 目标 QIODevice 在传输中被销毁"));
            return;
        }

        if (!safeDevice->isWritable()) {
            reply->abortWithError(NetworkError::InvalidRequest,
                                  QStringLiteral("downloadToDevice: 目标 QIODevice 已不可写"));
            return;
        }

        auto data = reply->readAll();
        if (data.has_value() && !data->isEmpty()) {
            const QByteArray &buf = data.value();
            qint64 totalWritten   = 0;
            while (totalWritten < buf.size()) {
                const qint64 written = safeDevice->write(buf.constData() + totalWritten,
                                                         buf.size() - totalWritten);
                if (written <= 0) {
                    reply->abortWithError(NetworkError::InvalidRequest,
                                          QStringLiteral("downloadToDevice: 写入目标设备失败: %1")
                                              .arg(safeDevice->errorString()));
                    return;
                }
                totalWritten += written;
            }
        }
    });

    reply->execute(); // 自动启动请求
    return reply;
}

QCNetworkReply *QCNetworkAccessManager::downloadFileResumable(const QUrl &url,
                                                              const QString &savePath,
                                                              bool overwrite)
{
    // 检查文件是否存在，用于断点续传
    QFile file(savePath);
    const bool hadExistingFile = file.exists();
    qint64 existingSize = 0;

    if (!overwrite && hadExistingFile) {
        existingSize = file.size();
        qDebug() << "File exists, resuming download from byte:" << existingSize;
    }

    QCNetworkRequest request(url);

    // 断点续传：设置 Range 头（RFC 7233）
    if (existingSize > 0) {
        QString rangeHeader = QStringLiteral("bytes=%1-").arg(existingSize);
        request.setRawHeader("Range", rangeHeader.toUtf8());
    }

    return dispatchSendRequest(
        request,
        HttpMethod::Get,
        true,
        Internal::makeEmptyRequestBody(),
        QByteArray(),
        "QCNetworkAccessManager::downloadFileResumable",
        [this, request, savePath, existingSize, hadExistingFile]() {
            const auto middlewaresSnapshot = middlewares();
            const QCNetworkRequest modifiedRequest =
                applyRequestPreSendMiddlewares(request, middlewaresSnapshot);
            auto *reply = createReply(modifiedRequest,
                                      HttpMethod::Get,
                                      true,
                                      Internal::makeEmptyRequestBody(),
                                      QByteArray(),
                                      this);

            wireResumableDownload(reply, reply->d_func(), savePath, existingSize, hadExistingFile);
            prepareManagedReply(reply, middlewaresSnapshot);
            startManagedReply(reply, modifiedRequest, true);
            return reply;
        });
}

// ==================
// 缓存管理实现
// ==================

void QCNetworkAccessManager::setCache(QCNetworkCache *cache)
{
    Q_D(QCNetworkAccessManager);
    if (d->autoCreatedCache && d->cache == d->autoCreatedCache && d->autoCreatedCache != cache) {
        d->autoCreatedCache->deleteLater();
        d->autoCreatedCache = nullptr;
    }
    d->cache = cache;
}

QCNetworkCache *QCNetworkAccessManager::cache() const
{
    Q_D(const QCNetworkAccessManager);
    return d->cache;
}

void QCNetworkAccessManager::setCachePath(const QString &path, qint64 maxSize)
{
    Q_D(QCNetworkAccessManager);
    if (d->autoCreatedCache && d->cache == d->autoCreatedCache) {
        d->autoCreatedCache->deleteLater();
    }
    auto *diskCache = new QCNetworkDiskCache(this);
    diskCache->setCacheDirectory(path);
    diskCache->setMaxCacheSize(maxSize);
    d->cache = diskCache;
    d->autoCreatedCache = diskCache;
}

// ==================
// 日志系统
// ==================

void QCNetworkAccessManager::setLogger(QCNetworkLogger *logger)
{
    Q_D(QCNetworkAccessManager);
    d->logger = logger;
}

QCNetworkLogger *QCNetworkAccessManager::logger() const
{
    Q_D(const QCNetworkAccessManager);
    return d->logger;
}

void QCNetworkAccessManager::setDebugTraceEnabled(bool enabled)
{
    Q_D(QCNetworkAccessManager);
    d->debugTraceEnabled = enabled;
}

bool QCNetworkAccessManager::debugTraceEnabled() const noexcept
{
    Q_D(const QCNetworkAccessManager);
    return d->debugTraceEnabled;
}

// ==================
// 中间件系统
// ==================

void QCNetworkAccessManager::addMiddleware(QCNetworkMiddleware *middleware)
{
    Q_D(QCNetworkAccessManager);
    if (!middleware) {
        return;
    }

    for (const auto &entry : d->middlewares) {
        if (middlewareEntryMatches(entry, middleware)) {
            return;
        }
    }

    d->middlewares.append(QCNetworkAccessManagerPrivate::MiddlewareEntry{middleware});
    middleware->registerManager(this);
}

void QCNetworkAccessManager::removeMiddleware(QCNetworkMiddleware *middleware)
{
    Q_D(QCNetworkAccessManager);
    if (!middleware) {
        return;
    }

    for (qsizetype i = d->middlewares.size() - 1; i >= 0; --i) {
        if (middlewareEntryMatches(d->middlewares.at(i), middleware)) {
            d->middlewares.removeAt(i);
        }
    }
    middleware->unregisterManager(this);
}

void QCNetworkAccessManager::clearMiddlewares()
{
    Q_D(QCNetworkAccessManager);
    for (const auto &entry : d->middlewares) {
        if (auto *middleware = middlewareFromEntry(entry)) {
            middleware->unregisterManager(this);
        }
    }
    d->middlewares.clear();
}

QList<QCNetworkMiddleware *> QCNetworkAccessManager::middlewares() const
{
    Q_D(const QCNetworkAccessManager);
    QList<QCNetworkMiddleware *> result;
    result.reserve(d->middlewares.size());
    for (const auto &entry : d->middlewares) {
        if (auto *middleware = middlewareFromEntry(entry)) {
            result.append(middleware);
        }
    }
    return sanitizeMiddlewares(result);
}

// ==================
// Mock 工具
// ==================

void QCNetworkAccessManager::setMockHandler(QCNetworkMockHandler *handler)
{
    Q_D(QCNetworkAccessManager);
    d->mockHandler = handler;
}

QCNetworkMockHandler *QCNetworkAccessManager::mockHandler() const
{
    Q_D(const QCNetworkAccessManager);
    return d->mockHandler;
}

} // namespace QCurl
