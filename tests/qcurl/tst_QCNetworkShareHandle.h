#ifndef TST_QCNETWORKSHAREHANDLE_H
#define TST_QCNETWORKSHAREHANDLE_H

#include <QObject>
#include <QProcess>
#include <QTemporaryDir>

namespace QCurl {
class QCNetworkReply;
}
using QCurl::QCNetworkReply;

class TestQCNetworkShareHandle : public QObject
{
    Q_OBJECT

public:
    TestQCNetworkShareHandle() = default;

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    void testCookieShareDisabledByDefault();
    void testCookieShareWithinManager();
    void testCookieShareIsolationAcrossManagers();
    void testCookieShareConcurrencySmoke();
    void tlsSessionCachesMatchNative_data();
    void tlsSessionCachesMatchNative();

private:
    Q_DISABLE_COPY_MOVE(TestQCNetworkShareHandle)

    [[nodiscard]] bool startObserveServer();
    void stopObserveServer();

    static bool waitForPortReady(quint16 port, int timeoutMs);
    static bool waitForFinished(QCNetworkReply *reply, int timeoutMs);

    QTemporaryDir m_tempDir;
    QProcess m_server;
    QString m_skipReason;
    quint16 m_port = 0;
    QString m_baseUrl;
};

#endif // TST_QCNETWORKSHAREHANDLE_H
