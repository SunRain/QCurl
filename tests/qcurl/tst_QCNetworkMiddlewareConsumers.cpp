#include "QCNetworkAccessManager.h"
#include "QCNetworkDownloadToDeviceJob.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkResumableDownloadJob.h"
#include "middleware_lifetime_test_support.h"
#include "qcnetwork_mock_test_support.h"

#include <QBuffer>
#include <QFile>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace QCurl;
using namespace MiddlewareLifetimeTestSupport;

namespace {

void destroyInPhase(CallbackMiddleware *middleware,
                    int phase,
                    const std::function<void(QCNetworkReply *)> &destroy)
{
    if (phase == 0) {
        middleware->callback = [destroy](int current) {
            if (current == 0) {
                destroy(nullptr);
            }
        };
    } else if (phase == 1) {
        middleware->created = destroy;
    } else {
        middleware->responded = destroy;
    }
}

void verifyDestructionResult(QCNetworkTransferJob *job,
                             const QSignalSpy &finished,
                             const QSignalSpy &failed,
                             const QString &target,
                             int phase)
{
    if (target == "job") {
        QVERIFY(!job);
        return;
    }
    QVERIFY(job);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(job->error(),
             phase == 0 || target == "device" ? NetworkError::InvalidRequest
                                              : NetworkError::OperationCancelled);
}

void startDownloadJob(QCNetworkTransferJob *job, bool resumable)
{
    if (resumable) {
        static_cast<QCNetworkResumableDownloadJob *>(job)->start();
    } else {
        static_cast<QCNetworkDownloadToDeviceJob *>(job)->start();
    }
}

} // namespace

class tst_QCNetworkMiddlewareConsumers : public QObject
{
    Q_OBJECT

public:
    tst_QCNetworkMiddlewareConsumers() = default;

private:
    Q_DISABLE_COPY_MOVE(tst_QCNetworkMiddlewareConsumers)

private Q_SLOTS:
    void unregisterAcrossConsumers_data();
    void unregisterAcrossConsumers();
    void factoryDestruction_data();
    void factoryDestruction();
    void downloadDestruction_data();
    void downloadDestruction();
};

void tst_QCNetworkMiddlewareConsumers::unregisterAcrossConsumers_data()
{
    QTest::addColumn<int>("consumer");
    for (int consumer = 0; consumer != 5; ++consumer) {
        QTest::newRow(qPrintable(QString::number(consumer))) << consumer;
    }
}

void tst_QCNetworkMiddlewareConsumers::unregisterAcrossConsumers()
{
    QFETCH(int, consumer);
    QCNetworkAccessManager manager;
    manager.enableRequestScheduler(consumer == 1);
    QCNetworkMockHandler mock;
    TestSupport::setMockHandler(manager, &mock);
    const QUrl url("http://example.com/middleware-consumer");
    mock.mockResponse(consumer == 2 ? HttpMethod::Custom : HttpMethod::Get, url, "payload", 200);
    CallbackMiddleware first;
    CallbackMiddleware second;
    first.created = [&](QCNetworkReply *) { manager.removeMiddleware(&second); };
    manager.addMiddleware(&first);
    manager.addMiddleware(&second);
    QBuffer device;
    QVERIFY(device.open(QIODevice::WriteOnly));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("download");
    if (consumer < 3) {
        auto *reply = consumer == 2 ? manager.sendCustomRequest(QCNetworkRequest(url), "OPTIONS")
                                    : manager.get(QCNetworkRequest(url));
        QVERIFY(reply);
        QSignalSpy finished(reply, &QCNetworkReply::finished);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(reply->error(), NetworkError::NoError);
    } else {
        std::unique_ptr<QCNetworkTransferJob> job;
        if (consumer == 3) {
            auto download = std::make_unique<QCNetworkDownloadToDeviceJob>(&manager, url, &device);
            download->start();
            job = std::move(download);
        } else {
            auto download = std::make_unique<QCNetworkResumableDownloadJob>(&manager, url, path);
            download->start();
            job = std::move(download);
        }
        QSignalSpy finished(job.get(), &QCNetworkTransferJob::finished);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(job->error(), NetworkError::NoError);
        if (consumer == 3) {
            QCOMPARE(device.data(), QByteArray("payload"));
        } else {
            QFile output(path);
            QVERIFY(output.open(QIODevice::ReadOnly));
            QCOMPARE(output.readAll(), QByteArray("payload"));
        }
    }
    QCOMPARE(first.calls, QList<int>({0, 1, 2}));
    QCOMPARE(second.calls, QList<int>({0}));
}

void tst_QCNetworkMiddlewareConsumers::factoryDestruction_data()
{
    QTest::addColumn<int>("phase");
    QTest::addColumn<bool>("destroyManager");
    for (int phase = 0; phase != 3; ++phase) {
        QTest::newRow(qPrintable(QStringLiteral("manager-%1").arg(phase))) << phase << true;
        if (phase != 0) {
            QTest::newRow(qPrintable(QStringLiteral("reply-%1").arg(phase))) << phase << false;
        }
    }
}

void tst_QCNetworkMiddlewareConsumers::factoryDestruction()
{
    QFETCH(int, phase);
    QFETCH(bool, destroyManager);
    auto manager = std::make_unique<QCNetworkAccessManager>();
    QCNetworkMockHandler mock;
    mock.setCaptureEnabled(true);
    TestSupport::setMockHandler(*manager, &mock);
    const QUrl url("http://example.com/middleware-destroy-factory");
    mock.mockResponse(HttpMethod::Get, url, "payload", 200);
    CallbackMiddleware first;
    CallbackMiddleware second;
    const auto destroy = [&](QCNetworkReply *reply) {
        if (destroyManager) {
            manager.reset();
        } else {
            delete reply;
        }
    };
    destroyInPhase(&first, phase, destroy);
    manager->addMiddleware(&first);
    manager->addMiddleware(&second);
    QPointer<QCNetworkReply> reply = manager->get(QCNetworkRequest(url));
    if (phase < 2) {
        QVERIFY(!reply);
        QCOMPARE(mock.takeCapturedRequests().size(), 0);
    } else {
        QTRY_VERIFY(reply.isNull());
    }
    QList<int> expected;
    for (int p = 0; p < phase; ++p) {
        expected.append(p);
    }
    QCOMPARE(second.calls, expected);
}

void tst_QCNetworkMiddlewareConsumers::downloadDestruction_data()
{
    QTest::addColumn<bool>("resumable");
    QTest::addColumn<int>("phase");
    QTest::addColumn<QString>("target");
    for (const bool resumable : {false, true}) {
        for (int phase = 0; phase != 3; ++phase) {
            for (const auto &target : {"manager", "job", "reply", "device"}) {
                if ((phase == 0 && QString(target) == "reply")
                    || (resumable && QString(target) == "device")) {
                    continue;
                }
                QTest::newRow(
                    qPrintable(QStringLiteral("%1-%2-%3").arg(resumable).arg(phase).arg(target)))
                    << resumable << phase << QString::fromLatin1(target);
            }
        }
    }
}

void tst_QCNetworkMiddlewareConsumers::downloadDestruction()
{
    QFETCH(bool, resumable);
    QFETCH(int, phase);
    QFETCH(QString, target);
    auto manager = std::make_unique<QCNetworkAccessManager>();
    auto device  = std::make_unique<QBuffer>();
    QVERIFY(device->open(QIODevice::WriteOnly));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QCNetworkMockHandler mock;
    mock.setCaptureEnabled(true);
    TestSupport::setMockHandler(*manager, &mock);
    const QUrl url("http://example.com/middleware-destroy-download");
    mock.mockResponse(HttpMethod::Get, url, "payload", 200);
    std::unique_ptr<QCNetworkTransferJob> job;
    if (resumable) {
        job = std::make_unique<QCNetworkResumableDownloadJob>(manager.get(),
                                                              url,
                                                              directory.filePath("output"));
    } else {
        job = std::make_unique<QCNetworkDownloadToDeviceJob>(manager.get(), url, device.get());
    }
    CallbackMiddleware middleware;
    CallbackMiddleware following;
    const auto destroy = [&](QCNetworkReply *reply) {
        if (target == "manager") {
            manager.reset();
        } else if (target == "job") {
            job.reset();
        } else if (target == "device") {
            device.reset();
        } else {
            delete reply;
        }
    };
    destroyInPhase(&middleware, phase, destroy);
    manager->addMiddleware(&middleware);
    manager->addMiddleware(&following);
    QSignalSpy finished(job.get(), &QCNetworkTransferJob::finished);
    QSignalSpy failed(job.get(), &QCNetworkTransferJob::failed);
    QPointer<QCNetworkTransferJob> safeJob(job.get());
    startDownloadJob(job.get(), resumable);
    QTRY_VERIFY(!safeJob || safeJob->isFinished());
    verifyDestructionResult(safeJob.data(), finished, failed, target, phase);
    if (target != "device") {
        QList<int> expected;
        for (int previous = 0; previous < phase; ++previous) {
            expected.append(previous);
        }
        QCOMPARE(following.calls, expected);
    }
    if (phase < 2) {
        QCOMPARE(mock.takeCapturedRequests().size(), 0);
        if (device) {
            QVERIFY(device->data().isEmpty());
        }
    }
}

QTEST_GUILESS_MAIN(tst_QCNetworkMiddlewareConsumers)
#include "tst_QCNetworkMiddlewareConsumers.moc"
