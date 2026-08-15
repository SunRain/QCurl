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

bool writeAllToDevice(QIODevice *device, const QByteArray &data)
{
    qint64 totalWritten = 0;
    while (totalWritten < data.size()) {
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

void QCNetworkDownloadToDeviceJob::doStart()
{
    Q_D(QCNetworkDownloadToDeviceJob);
    if (isFinished()) {
        return;
    }

    if (!d->manager) {
        fail(NetworkError::InvalidRequest,
             QStringLiteral("QCNetworkDownloadToDeviceJob: manager 为空"));
        return;
    }

    auto *manager = d->manager.data();
    auto *device  = d->device.data();
    if (thread() != manager->thread()) {
        fail(NetworkError::InvalidRequest,
             QStringLiteral("QCNetworkDownloadToDeviceJob: job 与 manager 不在同一线程"));
        return;
    }

    if (!device) {
        fail(NetworkError::InvalidRequest, invalidDeviceMessage());
        return;
    }

    if (device->thread() != manager->thread()) {
        fail(NetworkError::InvalidRequest,
             QStringLiteral(
                 "QCNetworkDownloadToDeviceJob: 目标 QIODevice 与 manager 不在同一线程"));
        return;
    }

    if (!device->isWritable()) {
        fail(NetworkError::InvalidRequest, invalidDeviceMessage());
        return;
    }

    auto *managerPrivate           = manager->d_func();
    const auto middlewaresSnapshot = manager->middlewares();
    const QCNetworkRequest preparedRequest
        = managerPrivate->prepareManagedRequest(d->request, middlewaresSnapshot);
    auto *networkReply = managerPrivate->createPreparedManagedReply(preparedRequest,
                                                                    HttpMethod::Get,
                                                                    Internal::makeEmptyRequestBody(),
                                                                    QByteArray(),
                                                                    middlewaresSnapshot);
    setReply(networkReply);
    if (!networkReply) {
        fail(NetworkError::InvalidRequest,
             QStringLiteral("QCNetworkDownloadToDeviceJob: 无法创建 reply"));
        return;
    }

    QPointer<QIODevice> safeDevice(device);
    QPointer<QCNetworkReply> safeReply(networkReply);
    QPointer<QCNetworkDownloadToDeviceJob> safeJob(this);
    QObject::connect(device, &QObject::destroyed, networkReply, [safeReply, safeJob]() {
        const QString message = deviceDestroyedMessage();
        // Commit the job-level result before reply teardown can make the generic
        // reply-destroyed observer report OperationCancelled.
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
    QObject::connect(networkReply, &QCNetworkReply::readyRead, this, [networkReply, safeDevice]() {
        if (!safeDevice) {
            networkReply->abortWithError(NetworkError::InvalidRequest, deviceDestroyedMessage());
            return;
        }
        if (!safeDevice->isWritable()) {
            networkReply
                ->abortWithError(NetworkError::InvalidRequest,
                                 QStringLiteral(
                                     "QCNetworkDownloadToDeviceJob: 目标 QIODevice 已不可写"));
            return;
        }

        const auto data = networkReply->readAll();
        if (!data.has_value() || data->isEmpty()) {
            return;
        }
        if (!writeAllToDevice(safeDevice.data(), data.value())) {
            networkReply->abortWithError(NetworkError::InvalidRequest,
                                         QStringLiteral(
                                             "QCNetworkDownloadToDeviceJob: 写入目标设备失败: %1")
                                             .arg(safeDevice->errorString()));
        }
    });

    QObject::connect(networkReply, &QCNetworkReply::finished, this, [this, networkReply]() {
        finishFromReply(networkReply);
    });
    if (networkReply->isFinished()) {
        finishFromReply(networkReply);
    }

    managerPrivate->startPreparedReply(networkReply, preparedRequest);
}

} // namespace QCurl
