/**
 * @file
 * @brief Tests explicit-start download-to-device job lifecycle.
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkDownloadToDeviceJob.h"
#include "QCNetworkError.h"
#include "QCNetworkMiddleware.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRequestPriority.h"
#include "QCNetworkRequestScheduler.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QEventLoop>
#include <QMetaType>
#include <QSignalSpy>
#include <QThread>
#include <QtTest/QtTest>

using namespace QCurl;

namespace {

class CountingMiddleware final : public QCNetworkMiddleware
{
public:
    void onRequestPreSend(QCNetworkRequest &request) override
    {
        Q_UNUSED(request);
        ++requestPreSendCount;
    }

    void onReplyCreated(QCNetworkReply *reply) override
    {
        Q_UNUSED(reply);
        ++replyCreatedCount;
    }

    void onResponseReceived(QCNetworkReply *reply) override
    {
        Q_UNUSED(reply);
        ++responseReceivedCount;
    }

    int requestPreSendCount   = 0;
    int replyCreatedCount     = 0;
    int responseReceivedCount = 0;
};

class FinishReplyOnCreatedMiddleware final : public QCNetworkMiddleware
{
public:
    void onReplyCreated(QCNetworkReply *reply) override
    {
        ++replyCreatedCount;
        reply->abortWithError(NetworkError::OperationCancelled,
                              QStringLiteral("middleware already finished reply"));
    }

    int replyCreatedCount = 0;
};

QUrl testUrl(const QString &path)
{
    return QUrl(QStringLiteral("http://download-job.test/%1").arg(path));
}

void processQueuedEvents()
{
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

void verifyNoSideEffects(const QCNetworkMockHandler &mock,
                         const CountingMiddleware &middleware,
                         const QSignalSpy &queuedSpy,
                         const QSignalSpy &startedSpy)
{
    QCOMPARE(mock.capturedRequests().size(), 0);
    QCOMPARE(middleware.requestPreSendCount, 0);
    QCOMPARE(middleware.replyCreatedCount, 0);
    QCOMPARE(middleware.responseReceivedCount, 0);
    QCOMPARE(queuedSpy.count(), 0);
    QCOMPARE(startedSpy.count(), 0);
}

void configureOneShotMock(QCNetworkMockHandler &mock, const QUrl &url, const QByteArray &body)
{
    mock.clear();
    mock.setCaptureEnabled(true);
    mock.setCaptureBodyPreviewLimit(128);
    mock.mockResponse(HttpMethod::Get, url, body, 200);
}

struct SideEffectHarness
{
    CountingMiddleware middleware;
    QCNetworkMockHandler mock;
    QCNetworkAccessManager manager;
    QCNetworkRequestScheduler *scheduler = nullptr;
    QSignalSpy queuedSpy;
    QSignalSpy startedSpy;

    SideEffectHarness()
        : scheduler(manager.scheduler())
        , queuedSpy(scheduler, &QCNetworkRequestScheduler::requestQueued)
        , startedSpy(scheduler, &QCNetworkRequestScheduler::requestStarted)
    {
        manager.addMiddleware(&middleware);
        manager.setMockHandler(&mock);
    }

    void verifyNoSideEffectsSeen() const
    {
        verifyNoSideEffects(mock, middleware, queuedSpy, startedSpy);
    }
};

} // namespace

class tst_QCNetworkDownloadToDeviceJob : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void constructorDoesNotStartRequest();
    void startQueuesDoStartAndStartsOnlyOnce();
    void nullDeviceFailsBeforeReplyCreation();
    void notWritableDeviceFailsBeforeReplyCreation();
    void crossThreadDeviceFailsBeforeReplyCreation();
    void crossThreadNotWritableDeviceReportsThreadErrorBeforeWritableProbe();
    void managerThreadMismatchFailsBeforeReplyCreation();
    void noEventDispatcherFailsSynchronously();
    void schedulerDoesNotQueueInvalidInput();
    void mockFastPathDoesNotOverrideInvalidInput();
    void alreadyFinishedReplyFromMiddlewareStillFinishesJobOnce();
    void destroyedReplyBeforeJobFinishedFailsJobOnce();
    void destroyedReplyAfterJobFinishedDoesNotEmitSecondTerminalSignal();
};

void tst_QCNetworkDownloadToDeviceJob::initTestCase()
{
    qRegisterMetaType<QCNetworkReply *>("QCNetworkReply*");
    qRegisterMetaType<QCNetworkRequestPriority>("QCurl::QCNetworkRequestPriority");
}

void tst_QCNetworkDownloadToDeviceJob::cleanup()
{
    QCNetworkRequestScheduler::instance()->cancelAllRequests();
}

void tst_QCNetworkDownloadToDeviceJob::constructorDoesNotStartRequest()
{
    SideEffectHarness harness;
    const QUrl url = testUrl(QStringLiteral("constructor"));
    configureOneShotMock(harness.mock, url, QByteArrayLiteral("body"));

    QBuffer device;
    QVERIFY(device.open(QIODevice::ReadWrite));

    QCNetworkDownloadToDeviceJob job(&harness.manager, url, &device);

    QCOMPARE(job.reply(), nullptr);
    QVERIFY(!job.isFinished());
    harness.verifyNoSideEffectsSeen();

    processQueuedEvents();
    QCOMPARE(job.reply(), nullptr);
    QVERIFY(!job.isFinished());
    harness.verifyNoSideEffectsSeen();
}

void tst_QCNetworkDownloadToDeviceJob::startQueuesDoStartAndStartsOnlyOnce()
{
    SideEffectHarness harness;
    const QUrl url           = testUrl(QStringLiteral("valid"));
    const QByteArray payload = QByteArrayLiteral("download-body");
    configureOneShotMock(harness.mock, url, payload);

    QBuffer device;
    QVERIFY(device.open(QIODevice::ReadWrite));

    QCNetworkDownloadToDeviceJob job(&harness.manager, url, &device);
    QSignalSpy failedSpy(&job, &QCNetworkTransferJob::failed);
    QSignalSpy finishedSpy(&job, &QCNetworkTransferJob::finished);

    job.start();
    job.start();

    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(finishedSpy.count(), 0);
    QCOMPARE(job.reply(), nullptr);
    QVERIFY(!job.isFinished());
    harness.verifyNoSideEffectsSeen();

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(job.error(), NetworkError::NoError);
    QVERIFY(job.reply() != nullptr);
    QCOMPARE(harness.middleware.requestPreSendCount, 1);
    QCOMPARE(harness.middleware.replyCreatedCount, 1);
    QCOMPARE(harness.middleware.responseReceivedCount, 1);
    QCOMPARE(harness.mock.capturedRequests().size(), 1);
    QCOMPARE(device.data(), payload);

    job.reply()->deleteLater();
}

void tst_QCNetworkDownloadToDeviceJob::nullDeviceFailsBeforeReplyCreation()
{
    SideEffectHarness harness;
    const QUrl url = testUrl(QStringLiteral("null-device"));
    configureOneShotMock(harness.mock, url, QByteArrayLiteral("should-not-run"));

    QCNetworkDownloadToDeviceJob job(&harness.manager, url, nullptr);
    QSignalSpy failedSpy(&job, &QCNetworkTransferJob::failed);
    QSignalSpy finishedSpy(&job, &QCNetworkTransferJob::finished);

    job.start();

    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(finishedSpy.count(), 0);
    QCOMPARE(job.reply(), nullptr);
    harness.verifyNoSideEffectsSeen();

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(job.error(), NetworkError::InvalidRequest);
    QCOMPARE(job.reply(), nullptr);
    harness.verifyNoSideEffectsSeen();
}

void tst_QCNetworkDownloadToDeviceJob::notWritableDeviceFailsBeforeReplyCreation()
{
    SideEffectHarness harness;
    const QUrl url = testUrl(QStringLiteral("not-writable"));
    configureOneShotMock(harness.mock, url, QByteArrayLiteral("should-not-run"));

    QBuffer device;
    QVERIFY(device.open(QIODevice::ReadOnly));

    QCNetworkDownloadToDeviceJob job(&harness.manager, url, &device);
    QSignalSpy failedSpy(&job, &QCNetworkTransferJob::failed);
    QSignalSpy finishedSpy(&job, &QCNetworkTransferJob::finished);

    job.start();

    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(finishedSpy.count(), 0);
    QCOMPARE(job.reply(), nullptr);
    harness.verifyNoSideEffectsSeen();

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(job.error(), NetworkError::InvalidRequest);
    QCOMPARE(job.reply(), nullptr);
    harness.verifyNoSideEffectsSeen();
}

void tst_QCNetworkDownloadToDeviceJob::crossThreadDeviceFailsBeforeReplyCreation()
{
    SideEffectHarness harness;
    const QUrl url = testUrl(QStringLiteral("cross-thread-device"));
    configureOneShotMock(harness.mock, url, QByteArrayLiteral("should-not-run"));

    QThread workerThread;
    workerThread.start();
    auto *device = new QBuffer;
    QVERIFY(device->open(QIODevice::ReadWrite));
    device->moveToThread(&workerThread);

    QCNetworkDownloadToDeviceJob job(&harness.manager, url, device);
    QSignalSpy failedSpy(&job, &QCNetworkTransferJob::failed);
    QSignalSpy finishedSpy(&job, &QCNetworkTransferJob::finished);

    job.start();

    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(finishedSpy.count(), 0);
    QCOMPARE(job.reply(), nullptr);
    harness.verifyNoSideEffectsSeen();

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(job.error(), NetworkError::InvalidRequest);
    QCOMPARE(job.reply(), nullptr);
    harness.verifyNoSideEffectsSeen();

    device->deleteLater();
    workerThread.quit();
    QVERIFY(workerThread.wait(1000));
}

void tst_QCNetworkDownloadToDeviceJob::
    crossThreadNotWritableDeviceReportsThreadErrorBeforeWritableProbe()
{
    SideEffectHarness harness;
    const QUrl url = testUrl(QStringLiteral("cross-thread-not-writable-device"));
    configureOneShotMock(harness.mock, url, QByteArrayLiteral("should-not-run"));

    QThread workerThread;
    workerThread.start();
    auto *device = new QBuffer;
    QVERIFY(device->open(QIODevice::ReadOnly));
    device->moveToThread(&workerThread);

    QCNetworkDownloadToDeviceJob job(&harness.manager, url, device);
    QSignalSpy failedSpy(&job, &QCNetworkTransferJob::failed);
    QSignalSpy finishedSpy(&job, &QCNetworkTransferJob::finished);

    job.start();

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(job.error(), NetworkError::InvalidRequest);
    QVERIFY(job.errorString().contains(QStringLiteral("同一线程")));
    QVERIFY(!job.errorString().contains(QStringLiteral("不可写")));
    QCOMPARE(job.reply(), nullptr);
    harness.verifyNoSideEffectsSeen();

    device->deleteLater();
    workerThread.quit();
    QVERIFY(workerThread.wait(1000));
}

void tst_QCNetworkDownloadToDeviceJob::managerThreadMismatchFailsBeforeReplyCreation()
{
    QThread workerThread;
    workerThread.start();

    auto *manager = new QCNetworkAccessManager;
    CountingMiddleware middleware;
    QCNetworkMockHandler mock;
    manager->addMiddleware(&middleware);
    manager->setMockHandler(&mock);
    manager->moveToThread(&workerThread);

    const QUrl url = testUrl(QStringLiteral("manager-thread"));
    configureOneShotMock(mock, url, QByteArrayLiteral("should-not-run"));

    QBuffer device;
    QVERIFY(device.open(QIODevice::ReadWrite));

    QCNetworkDownloadToDeviceJob job(manager, url, &device);
    QSignalSpy failedSpy(&job, &QCNetworkTransferJob::failed);
    QSignalSpy finishedSpy(&job, &QCNetworkTransferJob::finished);

    job.start();

    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(finishedSpy.count(), 0);
    QCOMPARE(job.reply(), nullptr);
    QCOMPARE(mock.capturedRequests().size(), 0);
    QCOMPARE(middleware.requestPreSendCount, 0);
    QCOMPARE(middleware.replyCreatedCount, 0);
    QCOMPARE(middleware.responseReceivedCount, 0);

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(job.error(), NetworkError::InvalidRequest);
    QCOMPARE(job.reply(), nullptr);
    QCOMPARE(mock.capturedRequests().size(), 0);
    QCOMPARE(middleware.requestPreSendCount, 0);
    QCOMPARE(middleware.replyCreatedCount, 0);
    QCOMPARE(middleware.responseReceivedCount, 0);

    manager->deleteLater();
    workerThread.quit();
    QVERIFY(workerThread.wait(1000));
}

void tst_QCNetworkDownloadToDeviceJob::noEventDispatcherFailsSynchronously()
{
    SideEffectHarness harness;
    const QUrl url = testUrl(QStringLiteral("no-event-dispatcher"));
    configureOneShotMock(harness.mock, url, QByteArrayLiteral("should-not-run"));

    QThread noLoopThread;
    QBuffer device;
    QVERIFY(device.open(QIODevice::ReadWrite));

    QCNetworkDownloadToDeviceJob job(&harness.manager, url, &device);
    job.moveToThread(&noLoopThread);

    QSignalSpy failedSpy(&job, &QCNetworkTransferJob::failed);
    QSignalSpy finishedSpy(&job, &QCNetworkTransferJob::finished);

    job.start();

    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(job.error(), NetworkError::InvalidRequest);
    QVERIFY(job.errorString().contains(QStringLiteral("事件循环")));
    QCOMPARE(job.reply(), nullptr);
    harness.verifyNoSideEffectsSeen();
}

void tst_QCNetworkDownloadToDeviceJob::schedulerDoesNotQueueInvalidInput()
{
    SideEffectHarness harness;
    harness.manager.enableRequestScheduler(true);

    const QUrl invalidUrl = testUrl(QStringLiteral("scheduler-invalid"));
    configureOneShotMock(harness.mock, invalidUrl, QByteArrayLiteral("should-not-run"));

    QCNetworkDownloadToDeviceJob invalidJob(&harness.manager, invalidUrl, nullptr);
    QSignalSpy invalidFinishedSpy(&invalidJob, &QCNetworkTransferJob::finished);
    invalidJob.start();

    QCOMPARE(invalidFinishedSpy.count(), 0);
    harness.verifyNoSideEffectsSeen();
    QTRY_COMPARE_WITH_TIMEOUT(invalidFinishedSpy.count(), 1, 1000);
    QCOMPARE(invalidJob.error(), NetworkError::InvalidRequest);
    harness.verifyNoSideEffectsSeen();

    const QUrl validUrl = testUrl(QStringLiteral("scheduler-valid"));
    configureOneShotMock(harness.mock, validUrl, QByteArrayLiteral("ok"));
    QBuffer validDevice;
    QVERIFY(validDevice.open(QIODevice::ReadWrite));
    QCNetworkDownloadToDeviceJob validJob(&harness.manager, validUrl, &validDevice);
    QSignalSpy validFinishedSpy(&validJob, &QCNetworkTransferJob::finished);
    validJob.start();

    QTRY_COMPARE_WITH_TIMEOUT(harness.queuedSpy.count(), 1, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(validFinishedSpy.count(), 1, 1000);
    QCOMPARE(validJob.error(), NetworkError::NoError);
    QCOMPARE(harness.mock.capturedRequests().size(), 1);
    QCOMPARE(harness.middleware.requestPreSendCount, 1);

    validJob.reply()->deleteLater();
}

void tst_QCNetworkDownloadToDeviceJob::mockFastPathDoesNotOverrideInvalidInput()
{
    SideEffectHarness harness;
    const QUrl url = testUrl(QStringLiteral("mock-fast-path"));
    configureOneShotMock(harness.mock, url, QByteArrayLiteral("mock-success"));

    QBuffer device;
    QVERIFY(device.open(QIODevice::ReadOnly));

    QCNetworkDownloadToDeviceJob job(&harness.manager, url, &device);
    QSignalSpy failedSpy(&job, &QCNetworkTransferJob::failed);
    QSignalSpy finishedSpy(&job, &QCNetworkTransferJob::finished);

    job.start();

    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(finishedSpy.count(), 0);
    harness.verifyNoSideEffectsSeen();

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(job.error(), NetworkError::InvalidRequest);
    QCOMPARE(job.reply(), nullptr);
    harness.verifyNoSideEffectsSeen();
}

void tst_QCNetworkDownloadToDeviceJob::alreadyFinishedReplyFromMiddlewareStillFinishesJobOnce()
{
    QCNetworkAccessManager manager;
    FinishReplyOnCreatedMiddleware middleware;
    QCNetworkMockHandler mock;
    mock.setCaptureEnabled(true);
    const QUrl url = testUrl(QStringLiteral("already-finished-from-middleware"));
    configureOneShotMock(mock, url, QByteArrayLiteral("should-not-run"));
    manager.addMiddleware(&middleware);
    manager.setMockHandler(&mock);

    QBuffer device;
    QVERIFY(device.open(QIODevice::ReadWrite));

    QCNetworkDownloadToDeviceJob job(&manager, url, &device);
    QSignalSpy failedSpy(&job, &QCNetworkTransferJob::failed);
    QSignalSpy finishedSpy(&job, &QCNetworkTransferJob::finished);

    job.start();

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(middleware.replyCreatedCount, 1);
    QCOMPARE(job.error(), NetworkError::OperationCancelled);
    QVERIFY(job.reply() != nullptr);
    QCOMPARE(mock.capturedRequests().size(), 0);

    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 1);

    job.reply()->deleteLater();
}

void tst_QCNetworkDownloadToDeviceJob::destroyedReplyBeforeJobFinishedFailsJobOnce()
{
    SideEffectHarness harness;
    harness.mock.setGlobalDelay(60000);
    const QUrl url = testUrl(QStringLiteral("destroyed-before-job-finished"));
    configureOneShotMock(harness.mock, url, QByteArrayLiteral("late-body"));

    QBuffer device;
    QVERIFY(device.open(QIODevice::ReadWrite));

    QCNetworkDownloadToDeviceJob job(&harness.manager, url, &device);
    QSignalSpy failedSpy(&job, &QCNetworkTransferJob::failed);
    QSignalSpy finishedSpy(&job, &QCNetworkTransferJob::finished);

    job.start();
    QTRY_VERIFY_WITH_TIMEOUT(job.reply() != nullptr, 1000);
    auto *reply = job.reply();
    reply->deleteLater();

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(job.error(), NetworkError::OperationCancelled);
    QVERIFY(job.errorString().contains(QStringLiteral("reply")));
    QCOMPARE(job.reply(), nullptr);

    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 1);
}

void tst_QCNetworkDownloadToDeviceJob::destroyedReplyAfterJobFinishedDoesNotEmitSecondTerminalSignal()
{
    SideEffectHarness harness;
    const QUrl url = testUrl(QStringLiteral("destroyed-after-job-finished"));
    configureOneShotMock(harness.mock, url, QByteArrayLiteral("body"));

    QBuffer device;
    QVERIFY(device.open(QIODevice::ReadWrite));

    QCNetworkDownloadToDeviceJob job(&harness.manager, url, &device);
    QSignalSpy failedSpy(&job, &QCNetworkTransferJob::failed);
    QSignalSpy finishedSpy(&job, &QCNetworkTransferJob::finished);

    job.start();
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    QCOMPARE(failedSpy.count(), 0);
    QVERIFY(job.reply() != nullptr);

    job.reply()->deleteLater();
    QTRY_COMPARE_WITH_TIMEOUT(job.reply(), nullptr, 1000);
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(job.error(), NetworkError::NoError);
}

QTEST_MAIN(tst_QCNetworkDownloadToDeviceJob)

#include "tst_QCNetworkDownloadToDeviceJob.moc"
