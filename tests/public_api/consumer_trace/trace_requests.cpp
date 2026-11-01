#include "../../qcurl/qcurl_http_script_server.h"
#include "trace_contract.h"

#include <QCNetworkAccessManager.h>
#include <QCNetworkDefaultLogger.h>
#include <QCNetworkHttpVersion.h>
#include <QCNetworkMiddlewareExtras.h>
#include <QCNetworkReply.h>
#include <QCNetworkRequest.h>
#include <QCNetworkSslConfig.h>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <array>

namespace {

constexpr int kRequestCount = 2;
using Replies               = std::array<QCurl::QCNetworkReply *, kRequestCount>;

QList<HttpScriptServer::Response> responses()
{
    QList<HttpScriptServer::Response> result;
    for (int i = 0; i < kRequestCount; ++i) {
        auto response = HttpScriptServer::response(
            200,
            "body",
            "X-Response-Marker: response-" + QByteArray::number(i)
                + "\r\n"
                  "Set-Cookie: session=response-secret; Path=/\r\nX-Auth-Token: response-secret\r\n"
                  "X-API-Key: response-secret\r\nX-Signature: response-secret\r\n");
        response.delayMs = 100;
        result.append(response);
    }
    return result;
}

QCurl::QCNetworkRequest traceRequest(const QUrl &url,
                                     bool http2,
                                     QCurl::QCNetworkDiagnosticQueryPolicy policy,
                                     int index)
{
    QCurl::QCNetworkRequest request(url);
    request.setDiagnosticQueryPolicy(policy);
    request.setHttpVersion(http2 ? QCurl::QCNetworkHttpVersion::Http2TLS
                                 : QCurl::QCNetworkHttpVersion::Http1_1);
    request.setSslConfig(QCurl::QCNetworkSslConfig::insecureConfig());
    for (const auto &name :
         {"Authorization", "Cookie", "X-Auth-Token", "X-API-Key", "X-Signature"}) {
        request.setRawHeader(name, "request-secret");
    }
    request.setRawHeader("X-Public-Id", "response-" + QByteArray::number(index));
    return request;
}

bool verifyReply(QCurl::QCNetworkReply *reply, bool http2, const QString &query, bool &overlapped)
{
    if (!reply->isFinished() || reply->error() != QCurl::NetworkError::NoError
        || reply->httpStatusCode() != 200 || reply->rawHeader("X-Response-Marker").isEmpty()
        || reply->rawHeader("Set-Cookie") != "session=response-secret; Path=/") {
        qCritical() << "网络终态或 raw accessor 不符合预期" << reply->errorString();
        return false;
    }
    if (http2) {
        const auto body        = reply->readAll();
        const auto echoed      = QJsonDocument::fromJson(body.value_or(QByteArray())).object();
        const auto queryObject = echoed.value(QStringLiteral("query")).toObject();
        overlapped |= echoed.value(QStringLiteral("activeStreams")).toInt() >= 2;
        if (echoed.value(QStringLiteral("httpVersion")).toString() != QStringLiteral("2.0")
            || queryObject.value(QStringLiteral("route")).toString() != query
            || queryObject.value(QStringLiteral("encoded")).toString() != QStringLiteral("+")) {
            qCritical() << "真实 HTTP/2 wire query 不符合预期";
            return false;
        }
    }
    return true;
}

bool verifyWire(const HttpScriptServer &server, const QStringList &targets)
{
    const auto wire = server.requests();
    for (const auto &target : targets) {
        const QByteArray expected = "GET " + target.toUtf8() + " HTTP/1.1\r\n";
        if (std::count_if(wire.cbegin(),
                          wire.cend(),
                          [&](const auto &request) { return request.startsWith(expected); })
            != 1) {
            qCritical() << "真实 HTTP/1 wire query 不符合预期";
            return false;
        }
    }
    return true;
}

bool observeReplies(const Replies &replies,
                    const HttpScriptServer &server,
                    bool http2,
                    TraceObservation &observation)
{
    bool overlapped = !http2 && server.peakRequests() >= 2;
    QSet<QString> responseMarkers;
    for (int i = 0; i < kRequestCount; ++i) {
        auto *reply         = replies[static_cast<size_t>(i)];
        const QString query = QStringLiteral("query-%1").arg(i);
        const QString expectedQuery = QStringLiteral("route=%1&encoded=%2B&delay_ms=100").arg(query);
        if (reply->url().query(QUrl::FullyEncoded) != expectedQuery
            || !verifyReply(reply, http2, query, overlapped)) {
            return false;
        }
        const QString marker = QString::fromLatin1(reply->rawHeader("X-Response-Marker"));
        if (!observation.requestMarkers.contains(marker) || responseMarkers.contains(marker)) {
            qCritical() << "响应 marker 不是夹具的唯一已知标记";
            return false;
        }
        responseMarkers.insert(marker);
        observation.responseMarkers.append(marker);
    }
    if (!overlapped) {
        qCritical() << "服务器未观察到请求重叠";
        return false;
    }
    qInfo() << "observed overlapping requests" << (http2 ? "HTTP/2" : "HTTP/1.1");
    return http2 || verifyWire(server, observation.targets);
}

Replies sendRequests(QCurl::QCNetworkAccessManager &manager,
                     const QUrl &base,
                     bool http2,
                     TraceObservation &observation,
                     QEventLoop &loop,
                     int &finished)
{
    Replies replies{};
    for (int i = 0; i < kRequestCount; ++i) {
        QUrl url = base;
        url.setQuery(QStringLiteral("route=query-%1&encoded=%2B&delay_ms=100").arg(i));
        observation.targets.append(url.path() + QLatin1Char('?') + url.query(QUrl::FullyEncoded));
        observation.requestMarkers.append(QStringLiteral("response-%1").arg(i));
        auto *reply = manager.get(traceRequest(url, http2, observation.policy, i));
        replies[static_cast<size_t>(i)] = reply;
        QObject::connect(reply, &QCurl::QCNetworkReply::finished, &loop, [&finished, &loop] {
            if (++finished == kRequestCount) {
                loop.quit();
            }
        });
    }
    return replies;
}

} // namespace

bool runTraceRequests(bool http2,
                      const QUrl &base,
                      bool idsEnabled,
                      QCurl::QCNetworkDiagnosticQueryPolicy policy)
{
    HttpScriptServer server(responses());
    if (!http2 && !server.start()) {
        qCritical() << "回环 HTTP/1 监听失败";
        return false;
    }
    QCurl::QCNetworkDefaultLogger *capture = nullptr;
    const auto logger = QCurl::QCNetworkLoggerHandle::createWithBorrow(&capture);
    capture->enableConsoleOutput(false);
    capture->setMinLogLevel(QCurl::NetworkLogLevel::Debug);
    QCurl::QCRedactingLoggingMiddleware middleware;
    QCurl::QCNetworkAccessManager manager;
    manager.addMiddleware(&middleware);
    manager.setLogger(logger);
    manager.setDebugTraceEnabled(true);
    TraceObservation observation{{}, {}, {}, {}, policy, idsEnabled};
    QEventLoop loop;
    int finished = 0;
    const auto replies
        = sendRequests(manager, http2 ? base : server.url(), http2, observation, loop, finished);
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    if (finished != kRequestCount) {
        loop.exec();
    }
    if (finished != kRequestCount || !observeReplies(replies, server, http2, observation)) {
        qCritical() << "真实并发请求未完成验证";
        return false;
    }
    observation.entries = capture->entries();
    return verifyTrace(observation);
}
