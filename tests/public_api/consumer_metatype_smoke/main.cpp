#include <QCGlobal.h>
#include <QCNetworkAccessManager.h>
#include <QCNetworkLaneCancelResult.h>
#include <QCNetworkLaneKey.h>
#include <QCNetworkRequestPriority.h>
#include <QCNetworkSchedulerPolicy.h>

#include <QCoreApplication>
#include <QMetaType>
#include <QString>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QCurl::initialize();

    const auto verifyType = [](const char *name, QMetaType byType) {
        const QMetaType byName = QMetaType::fromName(name);
        return byName.isValid() && byType.isValid() && byName.id() == byType.id()
               && QString::fromLatin1(byType.name()) == QString::fromLatin1(name);
    };

    if (!verifyType("QCurl::QCNetworkRequestPriority",
                    QMetaType::fromType<QCurl::QCNetworkRequestPriority>())
        || !verifyType("QCurl::QCNetworkLaneKey",
                       QMetaType::fromType<QCurl::QCNetworkLaneKey>())
        || !verifyType("QCurl::QCNetworkSchedulerPolicy",
                       QMetaType::fromType<QCurl::QCNetworkSchedulerPolicy>())
        || !verifyType("QCurl::QCNetworkSchedulerPolicy::LaneConfig",
                       QMetaType::fromType<QCurl::QCNetworkSchedulerPolicy::LaneConfig>())
        || !verifyType("QCurl::QCNetworkSchedulerStatistics",
                       QMetaType::fromType<QCurl::QCNetworkSchedulerStatistics>())
        || !verifyType("QCurl::QCNetworkLaneCancelResult",
                       QMetaType::fromType<QCurl::QCNetworkLaneCancelResult>())) {
        return 1;
    }

    return 0;
}
