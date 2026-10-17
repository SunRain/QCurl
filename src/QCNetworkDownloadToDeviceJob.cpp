#include "QCNetworkDownloadToDeviceJob.h"

#include "QCNetworkAccessManager.h"
#include "QCNetworkAccessManager_p.h"
#include "QCNetworkBody.h"
#include "QCNetworkHttpMethod.h"
#include "QCNetworkMiddleware.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "private/QCRequestPipeline_p.h"
#include "private/QCThreading_p.h"

#include <QIODevice>
#include <QPointer>
#include <QTimer>

namespace QCurl {

/**
 * @brief 保存下载到设备任务的借用对象和延迟启动状态。
 *
 * manager 与 device 均通过 QPointer 观察；实际启动排入任务对象所属线程的事件循环。
 */
class QCNetworkDownloadToDeviceJobPrivate
{
public:
    QPointer<QCNetworkAccessManager> manager; ///< 仅观察调用方持有的 manager。
    QCNetworkRequest request;
    QPointer<QIODevice> device;  ///< 仅观察调用方持有的目标设备。
    bool startRequested = false; ///< 防止重复排入事件循环。
};

namespace {

bool writeAllToDevice(QIODevice *device, QCNetworkDownloadToDeviceJob *job, const QByteArray &data)
{
    const QPointer<QIODevice> safeDevice(device);
    const QPointer<QCNetworkDownloadToDeviceJob> safeJob(job);
    qint64 totalWritten = 0;
    while (totalWritten < data.size()) {
        if (!safeDevice || !safeJob || safeJob->isFinished()) {
            return false;
        }
        const qint64 written = device->write(data.constData() + totalWritten,
                                             data.size() - totalWritten);
        if (written <= 0) {
            return false;
        }
        totalWritten += written;
    }
    return true;
}

QString invalidDeviceMessage()
{
    return QStringLiteral("QCNetworkDownloadToDeviceJob: 目标 QIODevice 为空或不可写");
}

QString noEventLoopMessage()
{
    return QStringLiteral("QCNetworkDownloadToDeviceJob: owner 线程缺少 Qt 事件循环，无法排队启动");
}

QString deviceDestroyedMessage()
{
    return QStringLiteral("QCNetworkDownloadToDeviceJob: 目标 QIODevice 在传输中被销毁");
}

} // namespace

QCNetworkDownloadToDeviceJob::QCNetworkDownloadToDeviceJob(QCNetworkAccessManager *manager,
                                                           const QUrl &url,
                                                           QIODevice *device,
                                                           QObject *parent)
    : QCNetworkDownloadToDeviceJob(manager, QCNetworkRequest(url), device, parent)
{}

QCNetworkDownloadToDeviceJob::QCNetworkDownloadToDeviceJob(QCNetworkAccessManager *manager,
                                                           const QCNetworkRequest &request,
                                                           QIODevice *device,
                                                           QObject *parent)
    : QCNetworkTransferJob(parent)
    , d_ptr(new QCNetworkDownloadToDeviceJobPrivate)
{
    Q_D(QCNetworkDownloadToDeviceJob);
    d->manager = manager;
    d->request = request;
    d->device  = device;
}

QCNetworkDownloadToDeviceJob::~QCNetworkDownloadToDeviceJob() = default;

void QCNetworkDownloadToDeviceJob::start()
{
    Q_D(QCNetworkDownloadToDeviceJob);
    if (d->startRequested || isFinished()) {
        return;
    }

    if (!Internal::hasEventDispatcher(thread())) {
        fail(NetworkError::InvalidRequest, noEventLoopMessage());
        return;
    }

    d->startRequested = true;
    QTimer::singleShot(0, this, &QCNetworkDownloadToDeviceJob::doStart);
}

bool QCNetworkDownloadToDeviceJob::validateStart()
{
    Q_D(QCNetworkDownloadToDeviceJob);
    if (!d->manager) {
        fail(NetworkError::InvalidRequest,
             QStringLiteral("QCNetworkDownloadToDeviceJob: manager 为空"));
        return false;
    }

    auto *manager = d->manager.data();
    auto *device  = d->device.data();
    if (thread() != manager->thread()) {
        fail(NetworkError::InvalidRequest,
             QStringLiteral("QCNetworkDownloadToDeviceJob: job 与 manager 不在同一线程"));
        return false;
    }

    if (!device) {
        fail(NetworkError::InvalidRequest, invalidDeviceMessage());
        return false;
    }

    if (device->thread() != manager->thread()) {
        fail(NetworkError::InvalidRequest,
             QStringLiteral(
                 "QCNetworkDownloadToDeviceJob: 目标 QIODevice 与 manager 不在同一线程"));
        return false;
    }

    if (!device->isWritable()) {
        fail(NetworkError::InvalidRequest, invalidDeviceMessage());
        return false;
    }

    return true;
}

void QCNetworkDownloadToDeviceJob::doStart()
{
    Q_D(QCNetworkDownloadToDeviceJob);
    if (isFinished() || !validateStart()) {
        return;
    }
    const QPointer<QCNetworkDownloadToDeviceJob> safeJob(this);
    const QPointer<QCNetworkAccessManager> safeManager(d->manager);
    const auto middlewaresSnapshot = safeManager->d_func()->middlewares;
    const auto preparedRequest = safeManager->d_func()->prepareManagedRequest(d->request,
                                                                              middlewaresSnapshot,
                                                                              this);
    if (!safeJob || isFinished()) {
        return;
    }
    if (!safeManager || !preparedRequest) {
        fail(NetworkError::InvalidRequest,
             QStringLiteral("QCNetworkDownloadToDeviceJob: manager 在请求准备期间失效"));
        return;
    }
    auto *networkReply = safeManager->d_func()
                             ->createPreparedManagedReply(*preparedRequest,
                                                          HttpMethod::Get,
                                                          Internal::makeEmptyRequestBody(),
                                                          QByteArray(),
                                                          middlewaresSnapshot,
                                                          this);
    if (!safeJob || isFinished()) {
        return;
    }
    if (!networkReply) {
        fail(NetworkError::OperationCancelled,
             QStringLiteral("QCNetworkDownloadToDeviceJob: reply 在中间件回调中失效"));
        return;
    }
    const QPointer<QCNetworkReply> safeReply(networkReply);
    if (!d->device || !d->device->isWritable()) {
        networkReply->deleteLater();
        fail(NetworkError::InvalidRequest, invalidDeviceMessage());
        return;
    }
    attachDownloadReply(networkReply);
    if (safeJob && !isFinished() && safeManager && safeReply) {
        safeManager->d_func()->startPreparedReply(safeReply.data(), *preparedRequest);
    }
}

void QCNetworkDownloadToDeviceJob::attachDownloadReply(QCNetworkReply *networkReply)
{
    Q_D(QCNetworkDownloadToDeviceJob);
    const QPointer<QIODevice> safeDevice(d->device);
    auto *device = safeDevice.data();
    setReply(networkReply);
    QPointer<QCNetworkReply> safeReply(networkReply);
    QPointer<QCNetworkDownloadToDeviceJob> safeJob(this);
    QObject::connect(device, &QObject::destroyed, networkReply, [safeReply, safeJob]() {
        const QString message = deviceDestroyedMessage();
        // 先提交任务终态，避免 reply 销毁观察器将设备错误覆盖为 OperationCancelled。
        if (safeJob) {
            safeJob->fail(NetworkError::InvalidRequest, message);
        }
        if (safeReply) {
            safeReply->abortWithError(NetworkError::InvalidRequest, message);
        }
    });

    QObject::connect(networkReply,
                     &QCNetworkReply::downloadProgress,
                     this,
                     [this](qint64 received, qint64 total) { Q_EMIT progress(received, total); });
    QObject::connect(networkReply,
                     &QCNetworkReply::readyRead,
                     this,
                     &QCNetworkDownloadToDeviceJob::writeAvailableData);

    QObject::connect(networkReply, &QCNetworkReply::finished, this, [this, networkReply]() {
        finishFromReply(networkReply);
    });
    if (networkReply->isFinished()) {
        finishFromReply(networkReply);
    }
}

void QCNetworkDownloadToDeviceJob::writeAvailableData()
{
    Q_D(QCNetworkDownloadToDeviceJob);
    const QPointer<QCNetworkDownloadToDeviceJob> safeJob(this);
    const QPointer<QCNetworkReply> safeReply(reply());
    const QPointer<QIODevice> safeDevice(d->device);
    if (isFinished() || !safeReply) {
        return;
    }
    if (!safeDevice || !safeDevice->isWritable()) {
        safeReply->abortWithError(NetworkError::InvalidRequest, invalidDeviceMessage());
        return;
    }
    const auto data = safeReply->readAll();
    if (!data || data->isEmpty()) {
        return;
    }
    if (!writeAllToDevice(safeDevice.data(), safeJob.data(), *data) && safeJob && !isFinished()
        && safeReply) {
        safeReply->abortWithError(NetworkError::InvalidRequest,
                                  QStringLiteral(
                                      "QCNetworkDownloadToDeviceJob: 写入目标设备失败: %1")
                                      .arg(safeDevice ? safeDevice->errorString()
                                                      : deviceDestroyedMessage()));
    }
}

} // namespace QCurl
