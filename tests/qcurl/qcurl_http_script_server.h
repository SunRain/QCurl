#ifndef QCURL_HTTP_SCRIPT_SERVER_H
#define QCURL_HTTP_SCRIPT_SERVER_H

#include <QHostAddress>
#include <QSharedPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

/// 本地真实 HTTP 夹具：记录请求，按顺序返回指定响应，生成大响应时不保留完整 body。
class HttpScriptServer final : public QObject
{
    Q_OBJECT

public:
    struct Response
    {
        QByteArray headers;
        QByteArray body;
        qint64 generatedBytes = 0;
        int delayMs           = 0;
    };

    explicit HttpScriptServer(QList<Response> responses, QObject *parent = nullptr)
        : QObject(parent)
        , m_responses(std::move(responses))
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (auto *socket = m_server.nextPendingConnection()) {
                acceptSocket(socket);
            }
        });
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost, 0); }
    QUrl url() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/resource").arg(m_server.serverPort()));
    }
    QList<QByteArray> requests() const { return m_requests; }
    int connectionCount() const { return m_connectionCount; }
    int peakRequests() const { return m_peakRequests; }
    int activeRequests() const { return m_activeRequests; }

    static Response response(int status, const QByteArray &body, const QByteArray &headers = {})
    {
        return {QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(status)
                    + QByteArrayLiteral(" Test\r\nContent-Length: ")
                    + QByteArray::number(body.size()) + QByteArrayLiteral("\r\n") + headers
                    + QByteArrayLiteral("Connection: close\r\n\r\n"),
                body};
    }

private:
    void acceptSocket(QTcpSocket *socket)
    {
        ++m_connectionCount;
        auto buffer = QSharedPointer<QByteArray>::create();
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer]() {
            buffer->append(socket->readAll());
            if (!buffer->contains("\r\n\r\n")) {
                return;
            }
            disconnect(socket, &QTcpSocket::readyRead, this, nullptr);
            m_requests.append(*buffer);
            ++m_activeRequests;
            m_peakRequests = qMax(m_peakRequests, m_activeRequests);
            connect(socket, &QTcpSocket::disconnected, this, [this]() { --m_activeRequests; });
            if (m_responses.isEmpty()) {
                socket->disconnectFromHost();
                return;
            }
            const auto index     = qMin(m_requests.size() - 1, m_responses.size() - 1);
            const Response reply = m_responses.at(index);
            QTimer::singleShot(reply.delayMs, socket, [this, socket, reply]() {
                sendResponse(socket, reply);
            });
        });
    }

    void sendResponse(QTcpSocket *socket, const Response &reply)
    {
        socket->write(reply.headers);
        socket->write(reply.body);
        auto remaining = QSharedPointer<qint64>::create(reply.generatedBytes);
        connect(socket, &QTcpSocket::bytesWritten, socket, [socket, remaining](qint64) {
            pumpResponse(socket, remaining);
        });
        pumpResponse(socket, remaining);
    }

    static void pumpResponse(QTcpSocket *socket, const QSharedPointer<qint64> &remaining)
    {
        if (socket->state() != QAbstractSocket::ConnectedState) {
            return;
        }
        if (*remaining > 0 && socket->bytesToWrite() < 65536) {
            const qint64 count = qMin<qint64>(*remaining, 65536);
            socket->write(QByteArray(count, 'x'));
            *remaining -= count;
        }
        if (*remaining == 0 && socket->bytesToWrite() == 0) {
            socket->disconnectFromHost();
        }
    }

    QTcpServer m_server;
    QList<Response> m_responses;
    QList<QByteArray> m_requests;
    int m_connectionCount = 0;
    int m_activeRequests = 0;
    int m_peakRequests   = 0;
};

#endif
