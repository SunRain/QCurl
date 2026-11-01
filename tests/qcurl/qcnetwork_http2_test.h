#ifndef QCNETWORK_HTTP2_TEST_H
#define QCNETWORK_HTTP2_TEST_H

#include "QCNetworkHttpVersion.h"

#include <QJsonObject>
#include <QMetaMethod>
#include <QObject>
#include <QProcess>
#include <QUrl>

namespace QCurl {
class QCNetworkAccessManager;
}

class TestQCNetworkHttp2 : public QObject
{
    Q_OBJECT

public:
    TestQCNetworkHttp2() = default;

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // 协议能力与协商
    void testHttp2Support();           // 检测 HTTP/2 支持
    void testHttp2Negotiation();       // HTTP/2 协议协商（ALPN）
    void testHttp2Multiplexing();      // 多路复用（单连接多请求）
    void testHttp2HeaderCompression(); // 头部压缩（HPACK）
    void testHttp2Downgrade();         // 协议降级到 HTTP/1.1
    void testHttp2WithSsl();           // HTTP/2 over TLS (h2)
    void testHttp2ConcurrentStreams(); // 并发流限制
    void testHttp2ConnectionReuse();   // 连接复用验证

    // 顺序请求基线
    void testHttp2VsHttp1Performance(); // 性能对比（简化版）
    void testSchedulerAdmissionAndStreamLimit_data();
    void testSchedulerAdmissionAndStreamLimit();
    void testDiagnosticQueryPolicy_data();
    void testDiagnosticQueryPolicy();
    void testDiagnosticQueryPolicyThroughProxy_data();
    void testDiagnosticQueryPolicyThroughProxy();

private:
    Q_DISABLE_COPY_MOVE(TestQCNetworkHttp2)
    static constexpr const char *kJsonErrorKey = "_qcurl_test_error";

    QCurl::QCNetworkAccessManager *m_manager = nullptr;

    QProcess m_localServer;
    QString m_h2BaseUrl;
    QString m_http1BaseUrl;
    QString m_localH2BaseUrl;
    QString m_localHttp1BaseUrl;
    QString m_serverError;
    bool m_disableDowngradeTest = false;

    // 辅助方法
    bool waitForSignal(QObject *obj, const QMetaMethod &signal, int timeout = 10000);
    bool checkHttp2Support(); // 检查编译期与运行期 HTTP/2 能力
    static bool waitForPortReady(quint16 port, int timeoutMs);
    bool startLocalTestServer();
    bool waitForLocalTestServerReady(QJsonObject *ready, QByteArray *output);
    bool configureLocalTestServer(const QJsonObject &ready, bool usesLocalH2, bool usesLocalHttp1);
    void stopLocalTestServer();
    QJsonObject requestJson(const QUrl &url,
                            QCurl::QCNetworkHttpVersion httpVersion,
                            const QList<QPair<QByteArray, QByteArray>> &headers = {},
                            int timeoutMs                                       = 15000);
    void requestConcurrentJson(int requestCount,
                               const QString &queryName,
                               int delayMs,
                               QList<QJsonObject> *responses);
    void verifySharedHttp2Session(const QList<QJsonObject> &responses);
    void runSequentialRequests(const QString &baseUrl,
                               QCurl::QCNetworkHttpVersion version,
                               const QString &queryName,
                               qint64 *elapsed);
};

#endif // QCNETWORK_HTTP2_TEST_H
