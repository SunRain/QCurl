/**
 * @file
 * @brief 实现 Blocking Extras 测试使用的本地上传回显服务器。
 */

#include "qcblocking_upload_echo_server.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

#include <mutex>
#include <thread>
#include <utility>

UploadEchoServer::UploadEchoServer(ResponsePlan plan)
    : m_plan(std::move(plan))
{
}

UploadEchoServer::~UploadEchoServer()
{
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool UploadEchoServer::start()
{
    return start(1);
}

bool UploadEchoServer::start(int expectedRequests)
{
    m_expectedRequests = expectedRequests;
    m_thread = std::thread([this]() { run(); });

    std::unique_lock<std::mutex> lock(m_mutex);
    m_ready.wait(lock, [this]() { return m_started.has_value(); });
    return m_started.value();
}

QUrl UploadEchoServer::url(const QString &path) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_port).arg(path));
}

UploadEchoServer::RequestRecord UploadEchoServer::lastRequest() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_requests.isEmpty()) {
        return {};
    }
    return m_requests.constLast();
}

void UploadEchoServer::run()
{
    QTcpServer server;
    const bool started = server.listen(QHostAddress::LocalHost, 0);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_started = started;
        m_port = started ? server.serverPort() : 0;
    }
    m_ready.notify_one();
    if (!started) {
        return;
    }

    for (int i = 0; i < m_expectedRequests; ++i) {
        if (!server.waitForNewConnection(30000)) {
            return;
        }

        handleSocket(server.nextPendingConnection());
    }
}

void UploadEchoServer::handleSocket(QTcpSocket *socket)
{
    if (!socket) {
        return;
    }
    const ParsedRequest request = readRequest(socket);
    if (!request.complete) {
        socket->disconnectFromHost();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_requests.append(
            RequestRecord{request.method, request.path, request.body, request.cookie, request.headers});
    }
    sendResponse(socket, request.body, m_plan);
    socket->disconnectFromHost();
    if (socket->state() != QAbstractSocket::UnconnectedState) {
        socket->waitForDisconnected(3000);
    }
}

QByteArray UploadEchoServer::headerValue(const QByteArray &line, const QByteArray &name)
{
    return line.mid(name.size() + 1).trimmed();
}

void UploadEchoServer::parseHeaders(ParsedRequest *request, const QByteArray &headerBlock)
{
    const QList<QByteArray> lines = headerBlock.split('\n');
    if (!lines.isEmpty()) {
        const QList<QByteArray> parts = lines.first().trimmed().split(' ');
        if (parts.size() >= 2) {
            request->method = parts.at(0).trimmed();
            request->path = parts.at(1).trimmed();
        }
    }

    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        if (line.toLower().startsWith(QByteArrayLiteral("cookie:"))) {
            request->cookie = headerValue(line, QByteArrayLiteral("cookie"));
        }
        const int colon = line.indexOf(':');
        if (colon > 0) {
            request->headers.append({line.left(colon).trimmed(), line.mid(colon + 1).trimmed()});
        }
        if (!line.toLower().startsWith(QByteArrayLiteral("content-length:"))) {
            continue;
        }
        bool ok = false;
        const qint64 value = headerValue(line, QByteArrayLiteral("content-length")).toLongLong(&ok);
        if (ok && value >= 0) {
            request->contentLength = value;
        }
    }
}

UploadEchoServer::ParsedRequest UploadEchoServer::readRequest(QTcpSocket *socket)
{
    QByteArray buffer;
    ParsedRequest request;
    int headerEnd = -1;
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < 30000) {
        if (!socket->bytesAvailable() && !socket->waitForReadyRead(100)) {
            continue;
        }
        buffer.append(socket->readAll());
        if (headerEnd < 0) {
            headerEnd = buffer.indexOf("\r\n\r\n");
            if (headerEnd >= 0) {
                parseHeaders(&request, buffer.left(headerEnd));
            }
        }
        if (headerEnd >= 0 && request.contentLength < 0 && request.method != QByteArrayLiteral("POST")
            && request.method != QByteArrayLiteral("PUT")
            && request.method != QByteArrayLiteral("PATCH")
            && request.method != QByteArrayLiteral("DELETE")) {
            request.complete = true;
            return request;
        }
        if (headerEnd < 0 || request.contentLength < 0) {
            continue;
        }

        const int bodyOffset = headerEnd + 4;
        const int needed = bodyOffset + static_cast<int>(request.contentLength);
        if (buffer.size() < needed) {
            continue;
        }
        request.body = buffer.mid(bodyOffset, static_cast<qsizetype>(request.contentLength));
        request.complete = true;
        return request;
    }

    return request;
}

void UploadEchoServer::sendResponse(QTcpSocket *socket,
                                    const QByteArray &requestBody,
                                    ResponsePlan plan)
{
    if (plan.body.isEmpty()) {
        plan.body = requestBody;
    }

    QByteArray response = plan.statusLine + QByteArrayLiteral("\r\nContent-Length: ")
        + QByteArray::number(plan.body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n");
    for (const QByteArray &header : plan.extraHeaders) {
        response += header + QByteArrayLiteral("\r\n");
    }
    response += QByteArrayLiteral("\r\n") + plan.body;
    socket->write(response);
    socket->flush();
    socket->waitForBytesWritten(30000);
}
