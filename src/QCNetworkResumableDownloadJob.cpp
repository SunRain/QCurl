#include "QCNetworkResumableDownloadJob.h"

#include "QCNetworkAccessManager.h"
#include "QCNetworkAccessManager_p.h"
#include "QCNetworkHttpMethod.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "private/QCNetworkResumableDownloadWriter_p.h"
#include "private/QCRequestPipeline_p.h"
#include "private/QCThreading_p.h"

#include <QFile>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QVariant>

namespace QCurl {
namespace {

QString noEventLoopMessage()
{
    return QStringLiteral(
        "QCNetworkResumableDownloadJob: owner 线程缺少 Qt 事件循环，无法排队启动");
}

} // namespace

class QCNetworkResumableDownloadJobPrivate
{
public:
    QPointer<QCNetworkAccessManager> manager;
    QCNetworkRequest request;
    QString savePath;
    bool overwrite      = false;
    bool startRequested = false;
    qint64 existingSize = 0;
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
                                                                    true,
                                                                    Internal::makeEmptyRequestBody(),
                                                                    QByteArray(),
                                                                    middlewaresSnapshot);
    setReply(networkReply);
    if (!networkReply) {
        fail(NetworkError::InvalidRequest,
             QStringLiteral("QCNetworkResumableDownloadJob: 无法创建 reply"));
        return;
    }

    networkReply->setProperty("_qcurl_resumable_existing_size",
                              QVariant::fromValue(d->existingSize));

    auto context = Internal::makeResumableDownloadWriteContext(d->savePath);

    const QString targetPath  = d->savePath;
    const qint64 existingSize = d->existingSize;
    QObject::connect(networkReply,
                     &QCNetworkReply::downloadProgress,
                     this,
                     [this](qint64 received, qint64 total) { emit progress(received, total); });
    QObject::connect(networkReply,
                     &QCNetworkReply::readyRead,
                     this,
                     [networkReply, context, targetPath, existingSize, hadExistingFile]() {
                         Internal::writeResumableDownloadChunk(networkReply,
                                                               context,
                                                               targetPath,
                                                               existingSize,
                                                               hadExistingFile);
                     });
    QObject::connect(networkReply,
                     &QCNetworkReply::finished,
                     this,
                     [this, networkReply, context, targetPath, existingSize, hadExistingFile]() {
                         if (const auto commitError = Internal::commitResumableDownloadIfNeeded(
                                 networkReply, context, targetPath, existingSize, hadExistingFile);
                             commitError.has_value()) {
                             fail(NetworkError::InvalidRequest, commitError.value());
                             return;
                         }

                         finishFromReply(networkReply);
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
                finishFromReply(safeReply.data());
            },
            Qt::QueuedConnection);
    }

    managerPrivate->startPreparedReply(networkReply, preparedRequest, true);
}

QCNetworkResumableDownloadJob::~QCNetworkResumableDownloadJob() = default;

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
