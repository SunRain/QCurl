#ifndef QCURL_TLS_TEST_SERVER_H
#define QCURL_TLS_TEST_SERVER_H

#include <QFile>
#include <QHostAddress>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>

#include <optional>
#include <utility>

class TlsTestTcpServer final : public QTcpServer
{
    Q_OBJECT

public:
    explicit TlsTestTcpServer(QObject *parent)
        : QTcpServer(parent)
    {}

Q_SIGNALS:
    void socketAccepted(qintptr descriptor);

protected:
    void incomingConnection(qintptr descriptor) override { Q_EMIT socketAccepted(descriptor); }

private:
    Q_DISABLE_COPY_MOVE(TlsTestTcpServer)
};

/// 使用仓内证书的本地 TLS 夹具；明文模式用于制造真实的 TLS 握手失败。
class LocalTlsServer final : public QObject
{
    Q_OBJECT

public:
    LocalTlsServer(QString certPath, QString keyPath, QObject *parent = nullptr)
        : QObject(parent)
        , m_certPath(std::move(certPath))
        , m_keyPath(std::move(keyPath))
    {
        connect(&m_server,
                &TlsTestTcpServer::socketAccepted,
                this,
                &LocalTlsServer::handleIncomingConnection);
    }

    [[nodiscard]] bool start(bool plaintext = false)
    {
        m_plaintext = plaintext;
        // 先发现后端再构造证书，避免 Qt 首次隐式加载与退出清理形成反向锁序。
        if (QSslSocket::availableBackends().isEmpty() || !QSslSocket::supportsSsl()
            || !loadFixture()) {
            if (m_errorString.isEmpty()) {
                m_errorString = QStringLiteral("Qt TLS 后端不可用");
            }
            return false;
        }
        if (!m_server.listen(QHostAddress::LocalHost, 0)) {
            m_errorString = m_server.errorString();
            return false;
        }
        return true;
    }

    quint16 port() const { return m_server.serverPort(); }
    int connectionCount() const { return m_connectionCount; }
    QString errorString() const { return m_errorString; }

private:
    Q_DISABLE_COPY_MOVE(LocalTlsServer)

    [[nodiscard]] bool loadFixture()
    {
        QFile certFile(m_certPath);
        if (!certFile.open(QIODevice::ReadOnly)) {
            m_errorString = QStringLiteral("无法读取 TLS 证书: %1").arg(m_certPath);
            return false;
        }
        QFile keyFile(m_keyPath);
        if (!keyFile.open(QIODevice::ReadOnly)) {
            m_errorString = QStringLiteral("无法读取 TLS 私钥: %1").arg(m_keyPath);
            return false;
        }
        m_certificate = QSslCertificate(certFile.readAll(), QSsl::Pem);
        m_privateKey  = QSslKey(keyFile.readAll(), QSsl::Rsa, QSsl::Pem);
        if (m_certificate->isNull() || m_privateKey->isNull()) {
            m_errorString = QStringLiteral("TLS 夹具解析失败: %1 / %2").arg(m_certPath, m_keyPath);
            return false;
        }
        m_errorString.clear();
        return true;
    }

    void handleIncomingConnection(qintptr descriptor)
    {
        auto *socket = new QSslSocket(this);
        if (!socket->setSocketDescriptor(descriptor)) {
            m_errorString = socket->errorString();
            socket->deleteLater();
            return;
        }
        ++m_connectionCount;
        connect(socket, &QAbstractSocket::disconnected, socket, &QObject::deleteLater);
        if (m_plaintext) {
            socket->write("HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }
        socket->setLocalCertificate(*m_certificate);
        socket->setPrivateKey(*m_privateKey);
        // 服务端不请求客户端证书；调用方的服务器证书校验保持开启。
        socket->setPeerVerifyMode(QSslSocket::VerifyNone);
        connect(socket, &QSslSocket::encrypted, socket, [socket]() {
            socket->disconnectFromHost();
        });
        socket->startServerEncryption();
    }

    TlsTestTcpServer m_server{this};
    QString m_certPath;
    QString m_keyPath;
    std::optional<QSslCertificate> m_certificate;
    std::optional<QSslKey> m_privateKey;
    int m_connectionCount = 0;
    bool m_plaintext      = false;
    QString m_errorString;
};

#endif
