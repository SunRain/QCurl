#include "QCCurlHandleManager.h"
#include "QCGlobal.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkConnectionPoolManager.h"
#include "QCNetworkDefaultLogger.h"
#include "QCNetworkDiagnosticQueryPolicy.h"
#include "QCNetworkError.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkSslConfig.h"
#include "qcnetwork_http2_test.h"
#include "qcnetwork_trace_mode.h"
#include "test_source_paths.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QProcess>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSet>
#include <QSharedPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QtTest/QtTest>

#include <curl/curl.h>

using namespace QCurl;

static const char *kHttp2TestBaseUrlEnvName = "QCURL_HTTP2_TEST_BASE_URL";
static const char *kHttp1TestBaseUrlEnvName = "QCURL_HTTP2_TEST_HTTP1_BASE_URL";
static const char *kDisableDowngradeEnvName = "QCURL_HTTP2_DISABLE_DOWNGRADE_TEST";

namespace {

bool outputSuggestsLocalListenRestriction(const QString &output)
{
    const QString normalized = output.toLower();
    return normalized.contains(QStringLiteral("listen eperm"))
           || normalized.contains(QStringLiteral("operation not permitted"))
           || normalized.contains(QStringLiteral("permission denied"));
}

} // namespace

bool TestQCNetworkHttp2::waitForSignal(QObject *obj, const QMetaMethod &signal, int timeout)
{
    // 避免 “信号已先触发 -> 新建 QSignalSpy 永远等不到” 的假超时。
    auto *reply = qobject_cast<QCNetworkReply *>(obj);
    if (signal == QMetaMethod::fromSignal(&QCNetworkReply::finished)) {
        if (reply && reply->state() == ReplyState::Finished) {
            return true;
        }
    }

    QSignalSpy spy(obj, signal);
    if (spy.count() > 0) {
        return true;
    }
    if (spy.wait(timeout)) {
        return true;
    }

    if (signal == QMetaMethod::fromSignal(&QCNetworkReply::finished)) {
        return reply && reply->state() == ReplyState::Finished;
    }

    return false;
}

bool TestQCNetworkHttp2::checkHttp2Support()
{
    const curl_version_info_data *ver = curl_version_info(CURLVERSION_NOW);
    return QCurl::hasHttp2Support() && ver && ((ver->features & CURL_VERSION_HTTP2) != 0);
}

bool TestQCNetworkHttp2::waitForPortReady(quint16 port, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        QTcpSocket probe;
        probe.connectToHost(QHostAddress::LocalHost, port);
        if (probe.waitForConnected(100)) {
            probe.disconnectFromHost();
            return true;
        }
        QThread::msleep(50);
    }

    return false;
}

bool TestQCNetworkHttp2::startLocalTestServer()
{
    const bool usesLocalH2    = m_h2BaseUrl.isEmpty() || m_h2BaseUrl == m_localH2BaseUrl;
    const bool usesLocalHttp1 = m_http1BaseUrl.isEmpty() || m_http1BaseUrl == m_localHttp1BaseUrl;
    stopLocalTestServer();
    m_serverError.clear();
    m_localH2BaseUrl.clear();
    m_localHttp1BaseUrl.clear();
    const QString scriptPath = TestSourcePaths::sourcePath(
        QStringLiteral("tests/qcurl/http2-test-server.js"));
    if (!QFileInfo::exists(scriptPath)) {
        m_serverError = QStringLiteral(
            "未找到本地 HTTP/2 测试服务器脚本（tests/qcurl/http2-test-server.js）。");
        return false;
    }
    m_localServer.setProgram(QStringLiteral("node"));
    m_localServer.setArguments({scriptPath,
                                QStringLiteral("--h2-port"),
                                QStringLiteral("0"),
                                QStringLiteral("--http1-port"),
                                QStringLiteral("0")});
    m_localServer.setProcessChannelMode(QProcess::MergedChannels);
    m_localServer.start();
    if (!m_localServer.waitForStarted(2000)) {
        m_serverError = QStringLiteral(
            "无法启动本地 HTTP/2 测试服务器（node）。请确认 node 可用。");
        return false;
    }
    QJsonObject ready;
    QByteArray output;
    if (!waitForLocalTestServerReady(&ready, &output)
        || !configureLocalTestServer(ready, usesLocalH2, usesLocalHttp1)) {
        const QString outText = QString::fromUtf8(output + m_localServer.readAll());
        if (!outText.isEmpty()) {
            qWarning().noquote() << "Local HTTP/2 server output:\n" << outText;
            if (outputSuggestsLocalListenRestriction(outText)) {
                m_serverError = QStringLiteral(
                    "当前执行环境禁止本地 HTTP/2 测试服务器监听 127.0.0.1");
            }
        }
        stopLocalTestServer();
        if (m_serverError.isEmpty()) {
            m_serverError = QStringLiteral("本地 HTTP/2 测试服务器未在预期时间内输出 READY 信号。");
        }
        return false;
    }
    return true;
}

bool TestQCNetworkHttp2::waitForLocalTestServerReady(QJsonObject *ready, QByteArray *output)
{
    QElapsedTimer timer;
    timer.start();
    const QRegularExpression expression(
        QStringLiteral(R"(QCURL_HTTP2_TEST_SERVER_READY\s+(\{.*\})\s*)"));
    while (timer.elapsed() < 5000) {
        if (!m_localServer.waitForReadyRead(200)) {
            continue;
        }
        *output += m_localServer.readAll();
        const QRegularExpressionMatch match = expression.match(QString::fromUtf8(*output));
        if (!match.hasMatch()) {
            continue;
        }
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(match.captured(1).toUtf8(),
                                                               &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            m_serverError = QStringLiteral("本地 HTTP/2 测试服务器输出格式错误：%1")
                                .arg(parseError.errorString());
            return false;
        }
        *ready = document.object();
        return true;
    }
    return false;
}

bool TestQCNetworkHttp2::configureLocalTestServer(const QJsonObject &ready,
                                                  bool usesLocalH2,
                                                  bool usesLocalHttp1)
{
    const int h2Port    = ready.value(QStringLiteral("h2Port")).toInt(0);
    const int http1Port = ready.value(QStringLiteral("http1Port")).toInt(0);
    if (h2Port <= 0 || http1Port <= 0) {
        m_serverError = QStringLiteral("本地 HTTP/2 测试服务器返回端口无效。");
        return false;
    }
    if (!waitForPortReady(static_cast<quint16>(h2Port), 3000)
        || !waitForPortReady(static_cast<quint16>(http1Port), 3000)) {
        m_serverError = QStringLiteral(
            "本地 HTTP/2 测试服务器未在预期时间内就绪（端口不可连接）。");
        return false;
    }
    m_localH2BaseUrl    = QStringLiteral("https://127.0.0.1:%1").arg(QString::number(h2Port));
    m_localHttp1BaseUrl = QStringLiteral("https://127.0.0.1:%1").arg(QString::number(http1Port));
    // 重启夹具后同步默认入口；外部覆盖的 URL 不变，不能继续连接已关闭的旧端口。
    if (usesLocalH2) {
        m_h2BaseUrl = m_localH2BaseUrl;
    }
    if (usesLocalHttp1) {
        m_http1BaseUrl = m_localHttp1BaseUrl;
    }
    return true;
}

void TestQCNetworkHttp2::stopLocalTestServer()
{
    if (m_localServer.state() == QProcess::NotRunning) {
        m_localH2BaseUrl.clear();
        m_localHttp1BaseUrl.clear();
        return;
    }

    m_localServer.terminate();
    if (!m_localServer.waitForFinished(1500)) {
        m_localServer.kill();
        m_localServer.waitForFinished(1500);
    }

    m_localH2BaseUrl.clear();
    m_localHttp1BaseUrl.clear();
}

QJsonObject TestQCNetworkHttp2::requestJson(const QUrl &url,
                                            QCNetworkHttpVersion httpVersion,
                                            const QList<QPair<QByteArray, QByteArray>> &headers,
                                            int timeoutMs)
{
    QCNetworkRequest request(url);
    request.setHttpVersion(httpVersion);
    request.setSslConfig(QCNetworkSslConfig::insecureConfig());

    for (const auto &kv : headers) {
        request.setRawHeader(kv.first, kv.second);
    }

    auto *reply = m_manager->get(request);
    if (!waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), timeoutMs)) {
        const QString err = reply->errorString();
        reply->cancel();
        reply->deleteLater();
        return QJsonObject{
            {QString::fromUtf8(kJsonErrorKey),
             QStringLiteral("请求超时（%1ms）：%2").arg(timeoutMs).arg(err)},
        };
    }

    if (reply->error() != NetworkError::NoError) {
        const QString err = reply->errorString();
        reply->deleteLater();
        return QJsonObject{
            {QString::fromUtf8(kJsonErrorKey), QStringLiteral("请求失败：%1").arg(err)},
        };
    }

    auto data = reply->readAll();
    reply->deleteLater();
    if (!data.has_value()) {
        return QJsonObject{
            {QString::fromUtf8(kJsonErrorKey), QStringLiteral("响应体为空（std::nullopt）")},
        };
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(*data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return QJsonObject{
            {QString::fromUtf8(kJsonErrorKey),
             QStringLiteral("JSON 解析失败：%1").arg(parseError.errorString())},
        };
    }
    if (!doc.isObject()) {
        return QJsonObject{
            {QString::fromUtf8(kJsonErrorKey), QStringLiteral("JSON 顶层必须是 object")},
        };
    }
    return doc.object();
}

void TestQCNetworkHttp2::initTestCase()
{
    QVERIFY2(checkHttp2Support(),
             "HTTP/2 capability preflight 应已阻止无 HTTP/2 能力的环境运行该 suite。");

    m_disableDowngradeTest = !qgetenv(kDisableDowngradeEnvName).trimmed().isEmpty()
                             && qgetenv(kDisableDowngradeEnvName).trimmed() != "0";

    const QString overriddenH2    = QString::fromUtf8(qgetenv(kHttp2TestBaseUrlEnvName)).trimmed();
    const QString overriddenHttp1 = QString::fromUtf8(qgetenv(kHttp1TestBaseUrlEnvName)).trimmed();

    if (!overriddenH2.isEmpty()) {
        m_h2BaseUrl = overriddenH2;
    }
    if (!overriddenHttp1.isEmpty()) {
        m_http1BaseUrl = overriddenHttp1;
    }

    if (m_h2BaseUrl.isEmpty() || m_http1BaseUrl.isEmpty()) {
        const bool localServerReady = startLocalTestServer();
        const QString reason = QStringLiteral("本地 HTTP/2 测试服务器不可用：%1").arg(m_serverError);
        QVERIFY2(localServerReady, qPrintable(reason));

        if (m_h2BaseUrl.isEmpty()) {
            m_h2BaseUrl = m_localH2BaseUrl;
        }
        if (m_http1BaseUrl.isEmpty()) {
            m_http1BaseUrl = m_localHttp1BaseUrl;
        }
    }

    qDebug() << "HTTP/2 base URL:" << m_h2BaseUrl;
    qDebug() << "HTTP/1.1 base URL:" << m_http1BaseUrl;

    m_manager = new QCNetworkAccessManager(this);
}

void TestQCNetworkHttp2::cleanupTestCase()
{
    stopLocalTestServer();
    m_manager = nullptr;
}

void TestQCNetworkHttp2::init() {}

void TestQCNetworkHttp2::cleanup() {}
