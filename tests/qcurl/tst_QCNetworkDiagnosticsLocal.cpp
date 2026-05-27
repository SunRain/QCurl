/**
 * @file tst_QCNetworkDiagnosticsLocal.cpp
 * @brief 本地 deterministic diagnostics gate
 *
 * 只依赖本进程内的 localhost fixture，覆盖 DNS、TLS、HTTP probe 和 diagnose
 * 的可复核合同，不使用公网探测。
 */

#include "QCNetworkDiagnostics.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QSharedPointer>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest/QtTest>

using namespace QCurl;

namespace {

QString diagMessage(const DiagResult &result)
{
    QString message = result.toString().trimmed();
    if (!result.details().isEmpty()) {
        const QJsonDocument detailsJson = QJsonDocument::fromVariant(result.details());
        if (!detailsJson.isNull()) {
            message += QStringLiteral("\nJSON details: %1")
                           .arg(QString::fromUtf8(detailsJson.toJson(QJsonDocument::Compact)));
        }
    }
    return message;
}

QByteArray buildHttpResponse(int statusCode, const QByteArray &reason, const QByteArray &body)
{
    QByteArray response;
    response += "HTTP/1.1 " + QByteArray::number(statusCode) + " " + reason + "\r\n";
    response += "Content-Type: text/plain\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;
    return response;
}

class LocalHttpServer final : public QObject
{
public:
    explicit LocalHttpServer(QObject *parent = nullptr)
        : QObject(parent)
    {
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (m_server.hasPendingConnections()) {
                QTcpSocket *socket = m_server.nextPendingConnection();
                if (!socket) {
                    continue;
                }
                setupSocket(socket);
            }
        });
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost, 0); }

    QUrl url(const QString &path) const
    {
        return QUrl(QStringLiteral("http://localhost:%1%2").arg(m_server.serverPort()).arg(path));
    }

    QString errorString() const { return m_server.errorString(); }
    int requestCount() const { return m_requestCount; }
    QByteArray lastPath() const { return m_lastPath; }

private:
    void setupSocket(QTcpSocket *socket)
    {
        auto requestBuffer = QSharedPointer<QByteArray>::create();
        auto handled       = QSharedPointer<bool>::create(false);

        QObject::connect(socket,
                         &QTcpSocket::readyRead,
                         socket,
                         [this, socket, requestBuffer, handled]() {
                             if (*handled) {
                                 return;
                             }

                             requestBuffer->append(socket->readAll());
                             const int headerEnd = requestBuffer->indexOf("\r\n\r\n");
                             if (headerEnd < 0) {
                                 return;
                             }

                             *handled = true;

                             const QByteArray requestLine
                                 = requestBuffer->left(headerEnd).split('\n').value(0).trimmed();
                             QList<QByteArray> parts = requestLine.split(' ');
                             const QByteArray rawPath = parts.size() >= 2 ? parts.at(1).trimmed()
                                                                          : QByteArrayLiteral("/");
                             const int queryPos       = rawPath.indexOf('?');
                             m_lastPath = queryPos >= 0 ? rawPath.left(queryPos) : rawPath;
                             ++m_requestCount;

                             const bool notFound = (m_lastPath == QByteArrayLiteral("/missing"));
                             const QByteArray response = notFound
                                                             ? buildHttpResponse(404,
                                                                                 "Not Found",
                                                                                 "missing")
                                                             : buildHttpResponse(200, "OK", "ok");
                             socket->write(response);
                             socket->flush();
                             socket->disconnectFromHost();
                         });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }

    QTcpServer m_server;
    int m_requestCount = 0;
    QByteArray m_lastPath;
};

class LocalTlsServer final : public QObject
{
public:
    LocalTlsServer(QString certPath, QString keyPath, QObject *parent = nullptr)
        : QObject(parent)
        , m_certPath(std::move(certPath))
        , m_keyPath(std::move(keyPath))
    {
        m_server.m_owner = this;
    }

    bool start()
    {
        if (!loadFixture()) {
            return false;
        }
        return m_server.listen(QHostAddress::LocalHost, 0);
    }

    quint16 port() const { return m_server.serverPort(); }
    int connectionCount() const { return m_connectionCount; }
    QString errorString() const { return m_errorString; }

private:
    class TlsTcpServer final : public QTcpServer
    {
    public:
        LocalTlsServer *m_owner = nullptr;

    protected:
        void incomingConnection(qintptr socketDescriptor) override
        {
            if (!m_owner) {
                QTcpServer::incomingConnection(socketDescriptor);
                return;
            }
            m_owner->handleIncomingConnection(socketDescriptor);
        }
    };

    bool loadFixture()
    {
        QFile certFile(m_certPath);
        if (!certFile.open(QIODevice::ReadOnly)) {
            m_errorString = QStringLiteral("无法读取 TLS 证书: %1").arg(m_certPath);
            return false;
        }
        const QByteArray certBytes = certFile.readAll();
        certFile.close();

        QFile keyFile(m_keyPath);
        if (!keyFile.open(QIODevice::ReadOnly)) {
            m_errorString = QStringLiteral("无法读取 TLS 私钥: %1").arg(m_keyPath);
            return false;
        }
        const QByteArray keyBytes = keyFile.readAll();
        keyFile.close();

        m_certificate = QSslCertificate(certBytes, QSsl::Pem);
        m_privateKey  = QSslKey(keyBytes, QSsl::Rsa, QSsl::Pem);
        if (m_certificate.isNull() || m_privateKey.isNull()) {
            m_errorString = QStringLiteral("TLS fixture 解析失败: %1 / %2")
                                .arg(m_certPath, m_keyPath);
            return false;
        }

        m_errorString.clear();
        return true;
    }

    void handleIncomingConnection(qintptr socketDescriptor)
    {
        auto *socket = new QSslSocket(this);
        if (!socket->setSocketDescriptor(socketDescriptor)) {
            m_errorString = socket->errorString();
            socket->deleteLater();
            return;
        }

        ++m_connectionCount;
        socket->setLocalCertificate(m_certificate);
        socket->setPrivateKey(m_privateKey);
        socket->setPeerVerifyMode(QSslSocket::VerifyNone);

        QObject::connect(socket, &QSslSocket::encrypted, socket, [socket]() {
            socket->disconnectFromHost();
        });
        QObject::connect(socket,
                         &QAbstractSocket::disconnected,
                         socket,
                         &QObject::deleteLater);

        socket->startServerEncryption();
    }

    TlsTcpServer m_server;
    QString m_certPath;
    QString m_keyPath;
    QSslCertificate m_certificate;
    QSslKey m_privateKey;
    int m_connectionCount = 0;
    QString m_errorString;
};

QString tlsFixturePath(const QString &fileName)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    return QDir(appDir).absoluteFilePath(QStringLiteral("../../tests/qcurl/testdata/http2/%1")
                                             .arg(fileName));
}

} // namespace

class tst_QCNetworkDiagnosticsLocal : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void testResolveDNSLocalhost();
    void testProbeHTTPLocalSuccess();
    void testProbeHTTPLocalNotFound();
    void testDiagnoseLocalHTTP();
    void testCheckSSLLocalFixture();

private:
    QString m_certPath;
    QString m_keyPath;
};

void tst_QCNetworkDiagnosticsLocal::initTestCase()
{
    m_certPath = tlsFixturePath(QStringLiteral("localhost.crt"));
    m_keyPath  = tlsFixturePath(QStringLiteral("localhost.key"));

    QVERIFY2(QFile::exists(m_certPath), qPrintable(QStringLiteral("缺少 TLS 证书: %1").arg(m_certPath)));
    QVERIFY2(QFile::exists(m_keyPath), qPrintable(QStringLiteral("缺少 TLS 私钥: %1").arg(m_keyPath)));
}

void tst_QCNetworkDiagnosticsLocal::testResolveDNSLocalhost()
{
    const auto result = QCNetworkDiagnostics::resolveDNS(QStringLiteral("localhost"), 1000);

    QVERIFY2(result.success(), qPrintable(diagMessage(result)));
    QCOMPARE(result.details().value(QStringLiteral("hostname")).toString(), QStringLiteral("localhost"));
    QVERIFY(result.details().value(QStringLiteral("ipv4")).toStringList().contains(
        QStringLiteral("127.0.0.1")));
}

void tst_QCNetworkDiagnosticsLocal::testProbeHTTPLocalSuccess()
{
    LocalHttpServer server;
    QVERIFY2(server.start(), qPrintable(server.errorString()));

    const auto result = QCNetworkDiagnostics::probeHTTP(server.url(QStringLiteral("/health")), 3000);

    QVERIFY2(result.success(), qPrintable(diagMessage(result)));
    QCOMPARE(result.details().value(QStringLiteral("statusCode")).toInt(), 200);
    QCOMPARE(server.requestCount(), 1);
    QCOMPARE(server.lastPath(), QByteArrayLiteral("/health"));
}

void tst_QCNetworkDiagnosticsLocal::testProbeHTTPLocalNotFound()
{
    LocalHttpServer server;
    QVERIFY2(server.start(), qPrintable(server.errorString()));

    const auto result = QCNetworkDiagnostics::probeHTTP(server.url(QStringLiteral("/missing")), 3000);

    QVERIFY2(!result.success(), qPrintable(diagMessage(result)));
    QVERIFY2(result.summary().contains(QStringLiteral("HTTP 探测失败")),
             qPrintable(diagMessage(result)));
    QCOMPARE(result.details().value(QStringLiteral("statusCode")).toInt(), 404);
    QCOMPARE(server.requestCount(), 1);
    QCOMPARE(server.lastPath(), QByteArrayLiteral("/missing"));
}

void tst_QCNetworkDiagnosticsLocal::testDiagnoseLocalHTTP()
{
    LocalHttpServer server;
    QVERIFY2(server.start(), qPrintable(server.errorString()));

    const auto result = QCNetworkDiagnostics::diagnose(server.url(QStringLiteral("/diagnose")));

    QVERIFY2(result.success(), qPrintable(diagMessage(result)));
    QCOMPARE(result.details().value(QStringLiteral("overallHealth")).toString(),
             QStringLiteral("excellent"));
    QVERIFY(result.details().contains(QStringLiteral("dns")));
    QVERIFY(result.details().contains(QStringLiteral("connection")));
    QVERIFY(result.details().contains(QStringLiteral("http")));
    QVERIFY(!result.details().contains(QStringLiteral("ssl")));

    const QVariantMap dnsResult = result.details().value(QStringLiteral("dns")).toMap();
    const QVariantMap connectionResult = result.details().value(QStringLiteral("connection")).toMap();
    const QVariantMap httpResult = result.details().value(QStringLiteral("http")).toMap();
    QVERIFY2(dnsResult.value(QStringLiteral("success")).toBool(), qPrintable(diagMessage(result)));
    QVERIFY2(connectionResult.value(QStringLiteral("success")).toBool(),
             qPrintable(diagMessage(result)));
    QVERIFY2(httpResult.value(QStringLiteral("success")).toBool(), qPrintable(diagMessage(result)));
    QCOMPARE(httpResult.value(QStringLiteral("statusCode")).toInt(), 200);
    QCOMPARE(server.lastPath(), QByteArrayLiteral("/diagnose"));
}

void tst_QCNetworkDiagnosticsLocal::testCheckSSLLocalFixture()
{
    LocalTlsServer server(m_certPath, m_keyPath);
    QVERIFY2(server.start(), qPrintable(server.errorString()));

    const auto result = QCNetworkDiagnostics::checkSSL(QStringLiteral("localhost"), server.port(), 4000);

    QVERIFY2(server.connectionCount() > 0, "本地 TLS fixture 未收到任何连接");
    QVERIFY(!result.details().value(QStringLiteral("timedOut")).toBool());

    if (result.success()) {
        QVERIFY2(result.details().value(QStringLiteral("verified")).toBool(),
                 qPrintable(diagMessage(result)));
        QVERIFY(!result.details().value(QStringLiteral("issuer")).toString().isEmpty());
        QVERIFY(!result.details().value(QStringLiteral("subject")).toString().isEmpty());
        return;
    }

    QVERIFY2(result.summary().contains(QStringLiteral("SSL 握手失败")),
             qPrintable(diagMessage(result)));
    QVERIFY(result.details().contains(QStringLiteral("sslErrors")) || !result.errorString().isEmpty());
}

QTEST_MAIN(tst_QCNetworkDiagnosticsLocal)
#include "tst_QCNetworkDiagnosticsLocal.moc"
