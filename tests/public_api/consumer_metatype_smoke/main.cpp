#include <QCCookie.h>
#include <QCCookieAsyncResult.h>
#include <QCGlobal.h>
#include <QCNetworkAccessManager.h>
#include <QCNetworkLaneCancelResult.h>
#include <QCNetworkLaneKey.h>
#include <QCNetworkRequestPriority.h>
#include <QCNetworkSchedulerPolicy.h>
#include <QCoreApplication>
#include <QEventLoop>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QTimer>

/**
 * @brief 通过真实 Qt 信号验证公共元类型的 queued 投递合同。
 */
class MetatypeProbe final : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

Q_SIGNALS:
    void priorityReady(QCurl::QCNetworkRequestPriority priority);

private:
    Q_DISABLE_COPY_MOVE(MetatypeProbe)
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QCurl::initialize();

    MetatypeProbe probe;
    QEventLoop eventLoop;
    bool delivered                                    = false;
    QCurl::QCNetworkRequestPriority deliveredPriority = QCurl::QCNetworkRequestPriority::Normal;
    const QMetaObject::Connection connection          = QObject::connect(
        &probe,
        &MetatypeProbe::priorityReady,
        &eventLoop,
        [&](QCurl::QCNetworkRequestPriority priority) {
            delivered         = true;
            deliveredPriority = priority;
            eventLoop.quit();
        },
        Qt::QueuedConnection);
    if (!connection) {
        return 1;
    }

    Q_EMIT probe.priorityReady(QCurl::QCNetworkRequestPriority::High);
    if (delivered) {
        return 2;
    }

    QTimer::singleShot(1000, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();
    if (!delivered || deliveredPriority != QCurl::QCNetworkRequestPriority::High) {
        return 3;
    }

    const auto verifyType = [](const char *name, QMetaType byType) {
        const QMetaType byName = QMetaType::fromName(name);
        return byName.isValid() && byType.isValid() && byName.id() == byType.id()
               && QString::fromLatin1(byType.name()) == QString::fromLatin1(name);
    };

    if (!verifyType("QCurl::QCCookie", QMetaType::fromType<QCurl::QCCookie>())
        || !verifyType("QCurl::QCCookieOperationResult",
                       QMetaType::fromType<QCurl::QCCookieOperationResult>())
        || !verifyType("QCurl::QCCookieExportResult",
                       QMetaType::fromType<QCurl::QCCookieExportResult>())
        || !verifyType("QCurl::QCNetworkRequestPriority",
                       QMetaType::fromType<QCurl::QCNetworkRequestPriority>())
        || !verifyType("QCurl::QCNetworkLaneKey", QMetaType::fromType<QCurl::QCNetworkLaneKey>())
        || !verifyType("QCurl::QCNetworkSchedulerPolicy",
                       QMetaType::fromType<QCurl::QCNetworkSchedulerPolicy>())
        || !verifyType("QCurl::QCNetworkSchedulerPolicy::LaneConfig",
                       QMetaType::fromType<QCurl::QCNetworkSchedulerPolicy::LaneConfig>())
        || !verifyType("QCurl::QCNetworkSchedulerStatistics",
                       QMetaType::fromType<QCurl::QCNetworkSchedulerStatistics>())
        || !verifyType("QCurl::QCNetworkLaneCancelResult",
                       QMetaType::fromType<QCurl::QCNetworkLaneCancelResult>())) {
        return 4;
    }

    return 0;
}

#include "main.moc"
