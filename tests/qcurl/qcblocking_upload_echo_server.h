/**
 * @file
 * @brief 声明 Blocking Extras 测试使用的本地上传回显服务器。
 */

#ifndef QCBLOCKING_UPLOAD_ECHO_SERVER_H
#define QCBLOCKING_UPLOAD_ECHO_SERVER_H

#include <QByteArray>
#include <QByteArrayList>
#include <QList>
#include <QPair>
#include <QUrl>

#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

class QTcpSocket;

class UploadEchoServer final
{
public:
    struct ResponsePlan
    {
        QByteArray statusLine = QByteArrayLiteral("HTTP/1.1 200 OK");
        QByteArray body;
        QByteArrayList extraHeaders = {QByteArrayLiteral("Set-Cookie: session=updated; Path=/")};
    };

    struct RequestRecord
    {
        QByteArray method;
        QByteArray path;
        QByteArray body;
        QByteArray cookie;
        QList<QPair<QByteArray, QByteArray>> headers;
    };

    UploadEchoServer() = default;
    explicit UploadEchoServer(ResponsePlan plan);
    ~UploadEchoServer();

    [[nodiscard]] bool start();
    [[nodiscard]] bool start(int expectedRequests);
    [[nodiscard]] QUrl url(const QString &path) const;
    [[nodiscard]] RequestRecord lastRequest() const;

private:
    struct ParsedRequest
    {
        bool complete = false;
        qint64 contentLength = -1;
        QByteArray method;
        QByteArray path;
        QByteArray body;
        QByteArray cookie;
        QList<QPair<QByteArray, QByteArray>> headers;
    };

    void run();

    static QByteArray headerValue(const QByteArray &line, const QByteArray &name);
    static void parseHeaders(ParsedRequest *request, const QByteArray &headerBlock);
    static ParsedRequest readRequest(QTcpSocket *socket);
    static void sendResponse(QTcpSocket *socket, const QByteArray &requestBody, ResponsePlan plan);
    void handleSocket(QTcpSocket *socket);

    mutable std::mutex m_mutex;
    std::condition_variable m_ready;
    std::optional<bool> m_started;
    std::thread m_thread;
    quint16 m_port = 0;
    int m_expectedRequests = 1;
    QList<RequestRecord> m_requests;
    ResponsePlan m_plan;
};

#endif // QCBLOCKING_UPLOAD_ECHO_SERVER_H
