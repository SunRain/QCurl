/**
 * @file QCNetworkDiagnosticsConnectivity.cpp
 * @brief 网络连接、SSL、HTTP 与综合诊断实现
 */

#include "QCNetworkDiagnostics.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslCertificate>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>

namespace QCurl {

namespace {

QVariantMap diagResultToVariantMap(const DiagResult &result)
{
    QVariantMap map = result.details();
    map.insert(QStringLiteral("success"), result.success());
    map.insert(QStringLiteral("summary"), result.summary());
    map.insert(QStringLiteral("durationMs"), result.durationMs());
    if (!result.errorString().isEmpty()) {
        map.insert(QStringLiteral("errorString"), result.errorString());
    }
    return map;
}

} // namespace

// ==================
// 连接测试
// ==================

DiagResult QCNetworkDiagnostics::testConnection(const QString &host, int port, int timeout)
{
    DiagResult result;
    result.setTimestamp(QDateTime::currentDateTime());

    QElapsedTimer timer;
    timer.start();

    QTcpSocket socket;
    QEventLoop loop;

    // 连接信号
    QObject::connect(&socket, &QTcpSocket::connected, &loop, &QEventLoop::quit);
    QObject::connect(&socket,
                     QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
                     &loop,
                     &QEventLoop::quit);

    // 超时定时器
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

    socket.connectToHost(host, port);
    timeoutTimer.start(timeout);

    loop.exec();

    result.setDurationMs(timer.elapsed());
    result.setDetail(QStringLiteral("host"), host);
    result.setDetail(QStringLiteral("port"), port);

    if (socket.state() == QAbstractSocket::ConnectedState) {
        result.setSuccess(true);
        result.setSummary(QStringLiteral("连接成功: %1:%2").arg(host).arg(port));
        result.setDetail(QStringLiteral("connected"), true);
        result.setDetail(QStringLiteral("connectDuration"), result.durationMs());
        result.setDetail(QStringLiteral("resolvedIP"), socket.peerAddress().toString());
        socket.close();
    } else {
        result.setSuccess(false);
        result.setSummary(QStringLiteral("连接失败: %1:%2").arg(host).arg(port));
        result.setDetail(QStringLiteral("connected"), false);
        result.setErrorString(socket.errorString());
    }

    return result;
}

// ==================
// SSL 检查
// ==================

DiagResult QCNetworkDiagnostics::checkSSL(const QString &host, int port, int timeout)
{
    DiagResult result;
    result.setTimestamp(QDateTime::currentDateTime());

    QElapsedTimer timer;
    timer.start();

    QSslSocket socket;
    QEventLoop loop;
    QList<QSslError> observedSslErrors;

    // 连接信号
    QObject::connect(&socket, &QSslSocket::encrypted, &loop, &QEventLoop::quit);
    QObject::connect(&socket,
                     QOverload<QAbstractSocket::SocketError>::of(&QSslSocket::errorOccurred),
                     &loop,
                     &QEventLoop::quit);
    QObject::connect(&socket,
                     QOverload<const QList<QSslError> &>::of(&QSslSocket::sslErrors),
                     &loop,
                     [&](const QList<QSslError> &errors) { observedSslErrors = errors; });

    // 超时定时器
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

    socket.connectToHostEncrypted(host, port);
    timeoutTimer.start(timeout);

    loop.exec();

    const bool timedOut = !timeoutTimer.isActive();
    timeoutTimer.stop();

    result.setDurationMs(timer.elapsed());
    result.setDetail(QStringLiteral("host"), host);
    result.setDetail(QStringLiteral("port"), port);

    if (socket.isEncrypted()) {
        QSslCertificate cert = socket.peerCertificate();

        result.setSuccess(true);
        result.setSummary(QStringLiteral("SSL 证书有效: %1").arg(host));
        result.setDetail(QStringLiteral("issuer"), cert.issuerDisplayName());
        result.setDetail(QStringLiteral("subject"), cert.subjectDisplayName());
        result.setDetail(QStringLiteral("notBefore"), cert.effectiveDate());
        result.setDetail(QStringLiteral("notAfter"), cert.expiryDate());

        int daysValid                = QDateTime::currentDateTime().daysTo(cert.expiryDate());
        result.setDetail(QStringLiteral("daysValid"), daysValid);
        result.setDetail(QStringLiteral("tlsVersion"), socket.sessionProtocol() == QSsl::TlsV1_3 ? QStringLiteral("TLSv1.3")
                                                        : QStringLiteral("TLSv1.2"));
        result.setDetail(QStringLiteral("verified"), socket.sslHandshakeErrors().isEmpty());

        socket.close();
    } else if (timedOut) {
        socket.abort();
        result.setSuccess(false);
        result.setSummary(QStringLiteral("SSL 握手超时: %1").arg(host));
        result.setErrorString(QStringLiteral("Timeout"));
        result.setDetail(QStringLiteral("timedOut"), true);
        result.setDetail(QStringLiteral("timeoutMs"), timeout);
    } else {
        result.setSuccess(false);
        result.setSummary(QStringLiteral("SSL 握手失败: %1").arg(host));
        result.setErrorString(socket.errorString());

        const QList<QSslError> allErrors = !observedSslErrors.isEmpty()
                                               ? observedSslErrors
                                               : socket.sslHandshakeErrors();
        if (!allErrors.isEmpty()) {
            QStringList errors;
            for (const QSslError &err : allErrors) {
                errors << err.errorString();
            }
            result.setDetail(QStringLiteral("sslErrors"), errors);
        }
    }

    return result;
}

// ==================
// HTTP 探测
// ==================

DiagResult QCNetworkDiagnostics::probeHTTP(const QUrl &url, int timeout)
{
    DiagResult result;
    result.setTimestamp(QDateTime::currentDateTime());

    QElapsedTimer timer;
    timer.start();

    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setTransferTimeout(timeout);

    QNetworkReply *reply = manager.get(request);
    QEventLoop loop;

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    loop.exec();

    result.setDurationMs(timer.elapsed());
    result.setDetail(QStringLiteral("url"), url.toString());
    result.setDetail(QStringLiteral("totalTime"), result.durationMs());
    result.setDetail(QStringLiteral("finalURL"), reply->url().toString());
    result.setDetail(QStringLiteral("networkError"), static_cast<int>(reply->error()));

    const QVariant statusCodeAttr = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (statusCodeAttr.isValid()) {
        result.setDetail(QStringLiteral("statusCode"), statusCodeAttr.toInt());
    }
    const QVariant statusTextAttr = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute);
    if (statusTextAttr.isValid()) {
        result.setDetail(QStringLiteral("statusText"), statusTextAttr.toString());
    }

    if (reply->error() == QNetworkReply::NoError) {
        result.setSuccess(true);
        result.setSummary(QStringLiteral("HTTP 探测成功: %1").arg(url.toString()));
    } else {
        result.setSuccess(false);
        result.setSummary(QStringLiteral("HTTP 探测失败: %1").arg(url.toString()));
        result.setErrorString(reply->errorString());
        result.setDetail(QStringLiteral("errorString"), result.errorString());
    }

    reply->deleteLater();
    return result;
}

// ==================
// 综合诊断
// ==================

DiagResult QCNetworkDiagnostics::diagnose(const QUrl &url)
{
    DiagResult result;
    result.setTimestamp(QDateTime::currentDateTime());

    QElapsedTimer timer;
    timer.start();

    QString host = url.host();
    int port     = url.port(url.scheme() == QStringLiteral("https") ? 443 : 80);
    bool isHttps = url.scheme() == QStringLiteral("https");

    bool hasSslWarning = false;

    // 1. DNS 解析
    DiagResult dnsResult  = resolveDNS(host);
    result.setDetail(QStringLiteral("dns"), QVariant::fromValue(diagResultToVariantMap(dnsResult)));

    if (!dnsResult.success()) {
        result.setSuccess(false);
        result.setSummary(QStringLiteral("诊断失败: DNS 解析失败"));
        result.setErrorString(dnsResult.errorString());
        result.setDurationMs(timer.elapsed());
        result.setDetail(QStringLiteral("failedStep"), QStringLiteral("dns"));
        result.setDetail(QStringLiteral("overallHealth"), QStringLiteral("error"));
        return result;
    }

    // 2. 连接测试
    DiagResult connResult        = testConnection(host, port);
    result.setDetail(QStringLiteral("connection"), QVariant::fromValue(diagResultToVariantMap(connResult)));

    if (!connResult.success()) {
        result.setSuccess(false);
        result.setSummary(QStringLiteral("诊断失败: 连接测试失败"));
        result.setErrorString(connResult.errorString());
        result.setDurationMs(timer.elapsed());
        result.setDetail(QStringLiteral("failedStep"), QStringLiteral("connection"));
        result.setDetail(QStringLiteral("overallHealth"), QStringLiteral("error"));
        return result;
    }

    // 3. SSL 检查（HTTPS）
    if (isHttps) {
        DiagResult sslResult  = checkSSL(host, port);
        result.setDetail(QStringLiteral("ssl"), QVariant::fromValue(diagResultToVariantMap(sslResult)));

        if (!sslResult.success()) {
            hasSslWarning = true;
        }
    }

    // 4. HTTP 探测
    DiagResult httpResult  = probeHTTP(url);
    result.setDetail(QStringLiteral("http"), QVariant::fromValue(diagResultToVariantMap(httpResult)));

    result.setDurationMs(timer.elapsed());

    if (httpResult.success()) {
        result.setSuccess(true);
        result.setSummary(hasSslWarning
                                              ? QStringLiteral("综合诊断完成（SSL 警告）: %1")
                                                    .arg(url.toString())
                                              : QStringLiteral("综合诊断完成: %1")
                                                    .arg(url.toString()));
        result.setDetail(QStringLiteral("overallHealth"), hasSslWarning
                                                              ? QStringLiteral("warning")
                                                              : QStringLiteral("excellent"));
    } else {
        result.setSuccess(false);
        result.setSummary(QStringLiteral("诊断失败: HTTP 探测失败"));
        result.setErrorString(httpResult.errorString());
        result.setDetail(QStringLiteral("failedStep"), QStringLiteral("http"));
        result.setDetail(QStringLiteral("overallHealth"), QStringLiteral("error"));
    }

    return result;
}

} // namespace QCurl
