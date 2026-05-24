#include <QCGlobal.h>
#include <QCNetworkRequestPriority.h>

#include <QCoreApplication>
#include <QMetaType>
#include <QString>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QCurl::initialize();

    const QMetaType byName = QMetaType::fromName("QCurl::QCNetworkRequestPriority");
    const QMetaType byType = QMetaType::fromType<QCurl::QCNetworkRequestPriority>();
    if (!byName.isValid() || !byType.isValid() || byName.id() != byType.id()) {
        return 1;
    }

    if (QString::fromLatin1(byType.name()) != QStringLiteral("QCurl::QCNetworkRequestPriority")) {
        return 2;
    }

    return 0;
}
