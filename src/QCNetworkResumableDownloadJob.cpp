#include "QCNetworkResumableDownloadJob.h"

#include "QCNetworkAccessManager.h"
#include "QCNetworkAccessManager_p.h"
#include "QCNetworkHttpMethod.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "QCNetworkRequest.h"
#include "private/QCNetworkResumableDownloadWriter_p.h"
#include "private/QCRequestPipeline_p.h"
#include "private/QCThreading_p.h"

#include <QFile>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QVariant>

#include <memory>

namespace QCurl {
namespace {

QString noEventLoopMessage()
{
    return QStringLiteral(
        "QCNetworkResumableDownloadJob: owner 线程缺少 Qt 事件循环，无法排队启动");
}

} // namespace

/**
 * @brief 保存断点续传任务的目标、策略和延迟启动状态。
 *
 * manager 仅被观察；启动与 reply 信号处理均依赖任务对象所属线程的事件循环。
 */
class QCNetworkResumableDownloadJobPrivate
{
public:
    QPointer<QCNetworkAccessManager> manager; ///< 仅观察调用方持有的 manager。
    QCNetworkRequest request;
    QString savePath;
    bool overwrite      = false;
    bool startRequested = false; ///< 防止重复排入事件循环。
    qint64 existingSize = 0;     ///< 发起 Range 请求前记录的本地文件长度。
    std::unique_ptr<Internal::ResumableDownloadWriter> writer;
};

QCNetworkResumableDownloadJob::QCNetworkResumableDownloadJob(QCNetworkAccessManager *manager,
                                                             const QUrl &url,
                                                             const QString &savePath,
                                                             bool overwrite,
                                                             QObject *parent)
    : QCNetworkResumableDownloadJob(manager, QCNetworkRequest(url), savePath, overwrite, parent)
{}

QCNetworkResumableDownloadJob::QCNetworkResumableDownloadJob(QCNetworkAccessManager *manager,
                                                             const QCNetworkRequest &request,
                                                             const QString &savePath,
                                                             bool overwrite,
                                                             QObject *parent)
    : QCNetworkTransferJob(parent)
    , d_ptr(new QCNetworkResumableDownloadJobPrivate)
{
    Q_D(QCNetworkResumableDownloadJob);
    d->manager   = manager;
    d->request   = request;
    d->savePath  = savePath;
    d->overwrite = overwrite;
}

void QCNetworkResumableDownloadJob::start()
{
    Q_D(QCNetworkResumableDownloadJob);
    if (d->startRequested || isFinished()) {
        return;
    }

    if (!Internal::hasEventDispatcher(thread())) {
        fail(NetworkError::InvalidRequest, noEventLoopMessage());
        return;
    }

    d->startRequested = true;
    QTimer::singleShot(0, this, &QCNetworkResumableDownloadJob::doStart);
}

void QCNetworkResumableDownloadJob::doStart()
{
    Q_D(QCNetworkResumableDownloadJob);
    if (isFinished()) {
        return;
    }

    if (!d->manager) {
        fail(NetworkError::InvalidRequest,
             QStringLiteral("QCNetworkResumableDownloadJob: manager 为空"));
        return;
    }

    auto *manager = d->manager.data();
    if (thread() != manager->thread()) {
        fail(NetworkError::InvalidRequest,
             QStringLiteral("QCNetworkResumableDownloadJob: job 与 manager 不在同一线程"));
        return;
    }

    QFile file(d->savePath);
    const bool hadExistingFile = file.exists();
    d->existingSize            = 0;
    if (!d->overwrite && hadExistingFile) {
        d->existingSize = file.size();
    }

    QCNetworkRequest downloadRequest(d->request);
    if (d->existingSize > 0) {
        downloadRequest.setRawHeader(QByteArrayLiteral("Range"),
                                     QStringLiteral("bytes=%1-").arg(d->existingSize).toUtf8());
    }

    auto *managerPrivate           = manager->d_func();
    const auto middlewaresSnapshot = manager->middlewares();
    const QCNetworkRequest preparedRequest
        = managerPrivate->prepareManagedRequest(downloadRequest, middlewaresSnapshot);
    auto *networkReply = managerPrivate->createPreparedManagedReply(preparedRequest,
                                                                    HttpMethod::Get,
                                                                    Internal::makeEmptyRequestBody(),
                                                                    QByteArray(),
                                                                    middlewaresSnapshot);
    if (!networkReply) {
        fail(NetworkError::InvalidRequest,
             QStringLiteral("QCNetworkResumableDownloadJob: 无法创建 reply"));
        return;
    }

    d->writer = std::make_unique<Internal::ResumableDownloadWriter>(d->savePath,
                                                                    d->existingSize,
                                                                    hadExistingFile);
    networkReply->d_func()->restoreResponseBeforeRetry = [this]() -> std::optional<QString> {
        Q_D(QCNetworkResumableDownloadJob);
        return d->writer ? d->writer->restoreBeforeRetry()
                         : std::optional<QString>(QStringLiteral("下载 writer 已释放"));
    };
    QObject::connect(networkReply, &QObject::destroyed, this, [this]() {
        Q_D(QCNetworkResumableDownloadJob);
        d->writer.reset();
    });
    setReply(networkReply);

    networkReply->setProperty("_qcurl_resumable_existing_size",
                              QVariant::fromValue(d->existingSize));

    QObject::connect(networkReply,
                     &QCNetworkReply::downloadProgress,
                     this,
                     [this](qint64 received, qint64 total) { Q_EMIT progress(received, total); });
    QObject::connect(networkReply, &QCNetworkReply::readyRead, this, [this, networkReply]() {
        Q_D(QCNetworkResumableDownloadJob);
        if (!d->writer || isFinished()) {
            return;
        }
        if (const auto error = d->writer->writeChunk(networkReply); error.has_value()) {
            networkReply->abortWithError(NetworkError::InvalidRequest, error.value());
        }
    });
    QObject::connect(networkReply, &QCNetworkReply::finished, this, [this, networkReply]() {
        handleReplyFinished(networkReply);
    });
    if (networkReply->isFinished()) {
        // 保留构造后再连接信号也能收到终态的使用合同。
        QPointer<QCNetworkReply> safeReply(networkReply);
        QMetaObject::invokeMethod(
            this,
            [this, safeReply]() {
                if (isFinished() || !safeReply) {
                    return;
                }
                handleReplyFinished(safeReply.data());
            },
            Qt::QueuedConnection);
    }

    managerPrivate->startPreparedReply(networkReply, preparedRequest);
}

QCNetworkResumableDownloadJob::~QCNetworkResumableDownloadJob()
{
    Q_D(QCNetworkResumableDownloadJob);
    Q_ASSERT(QThread::currentThread() == thread());
    if (auto *networkReply = reply()) {
        networkReply->d_func()->restoreResponseBeforeRetry = nullptr;
        QObject::disconnect(networkReply, nullptr, this, nullptr);
    }
    d->writer.reset();
}

void QCNetworkResumableDownloadJob::handleReplyFinished(QCNetworkReply *networkReply)
{
    Q_D(QCNetworkResumableDownloadJob);
    if (isFinished()) {
        d->writer.reset();
        return;
    }

    std::optional<QString> commitError;
    if (d->writer) {
        commitError = d->writer->commitIfNeeded(networkReply);
    }
    d->writer.reset();
    if (commitError.has_value()) {
        fail(NetworkError::InvalidRequest, commitError.value());
        return;
    }
    finishFromReply(networkReply);
}

QString QCNetworkResumableDownloadJob::savePath() const
{
    Q_D(const QCNetworkResumableDownloadJob);
    return d->savePath;
}

qint64 QCNetworkResumableDownloadJob::existingSize() const noexcept
{
    Q_D(const QCNetworkResumableDownloadJob);
    return d->existingSize;
}

} // namespace QCurl
