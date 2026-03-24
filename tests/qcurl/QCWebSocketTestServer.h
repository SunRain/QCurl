#ifndef QCURL_TEST_QCWEBSOCKETTESTSERVER_H
#define QCURL_TEST_QCWEBSOCKETTESTSERVER_H

/**
 * @file QCWebSocketTestServer.h
 * @brief 启动和管理 Qt Test 使用的本地 WebSocket server。
 *
 * 默认封装两类本地 server：
 * - Fragment Echo：依赖 `tests/qcurl/websocket-fragment-server.js`
 * - Evidence Server：依赖 `tests/qcurl/websocket-evidence-server.js`
 *
 * 该 helper 负责 READY marker 解析、端口连通性探测和进程回收。
 */

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QThread>

QT_BEGIN_NAMESPACE
namespace QCurl {

class QCWebSocketTestServer
{
public:
    /// @brief 连接协议模式。
    enum class Mode {
        Ws,
        Wss,
    };

    /// @brief 本地 server 实现类型。
    enum class ServerKind {
        FragmentEcho,
        Evidence,
    };

    /// @brief 构造一个尚未启动的测试 server helper。
    QCWebSocketTestServer() = default;

    /// @brief 停止仍在运行的测试 server。
    ~QCWebSocketTestServer() { stop(); }

    QCWebSocketTestServer(const QCWebSocketTestServer &)            = delete;
    QCWebSocketTestServer &operator=(const QCWebSocketTestServer &) = delete;

    /// @brief 返回最近一次启动失败或跳过的原因。
    /// @return 失败原因；为空表示当前无额外说明。
    [[nodiscard]] QString skipReason() const { return m_skipReason; }

    /// @brief 返回已启动 server 监听的本地端口。
    /// @return 监听端口；未启动时返回 0。
    [[nodiscard]] quint16 port() const { return m_port; }

    /// @brief 返回 evidence server 输出工件目录。
    /// @return 工件目录；非 evidence 模式或未启动时为空。
    [[nodiscard]] QString artifactsPath() const { return m_artifactsPath; }

    /// @brief 返回当前 server 的 base URL。
    /// @return `ws://` 或 `wss://` 开头的本地 URL；未启动时为空。
    [[nodiscard]] QString baseUrl() const
    {
        if (m_port == 0) {
            return {};
        }
        const QString scheme = (m_mode == Mode::Wss) ? QStringLiteral("wss") : QStringLiteral("ws");
        return QStringLiteral("%1://localhost:%2").arg(scheme).arg(m_port);
    }

    /// @brief 在 base URL 后拼接路径。
    /// @param path 以 `/` 开头或不带前导 `/` 的相对路径。
    /// @return 拼接后的 URL；未启动时为空。
    [[nodiscard]] QString urlWithPath(const QString &path) const
    {
        if (m_port == 0) {
            return {};
        }

        QString p = path;
        if (!p.isEmpty() && !p.startsWith('/')) {
            p.prepend('/');
        }
        return baseUrl() + p;
    }

    /// @brief 返回本地 WSS 测试证书路径。
    /// @return `tests/qcurl/testdata/http2/localhost.crt` 的绝对路径。
    [[nodiscard]] QString caCertPath() const
    {
        const QString appDir = QCoreApplication::applicationDirPath();
        return QDir(appDir).absoluteFilePath(
            QStringLiteral("../../tests/qcurl/testdata/http2/localhost.crt"));
    }

    /// @brief 启动指定模式和实现类型的本地 server。
    /// @param mode `ws` 或 `wss`。
    /// @param kind 选择 fragment echo 或 evidence server。
    /// @return 启动并就绪返回 `true`，否则返回 `false` 并设置 skipReason()。
    bool start(Mode mode, ServerKind kind = ServerKind::FragmentEcho)
    {
        stop();

        m_mode = mode;
        m_kind = kind;
        m_port = 0;
        m_artifactsPath.clear();
        m_skipReason = {};

        const QString appDir     = QCoreApplication::applicationDirPath();
        const QString scriptPath = QDir(appDir).absoluteFilePath(
            (m_kind == ServerKind::Evidence)
                ? QStringLiteral("../../tests/qcurl/websocket-evidence-server.js")
                : QStringLiteral("../../tests/qcurl/websocket-fragment-server.js"));
        if (!QFileInfo::exists(scriptPath)) {
            m_skipReason = QStringLiteral("未找到本地 WebSocket 测试服务器脚本：%1").arg(scriptPath);
            return false;
        }

        QStringList args{scriptPath, QStringLiteral("--port"), QStringLiteral("0")};
        if (m_mode == Mode::Wss) {
            const QString certPath = QDir(appDir).absoluteFilePath(
                QStringLiteral("../../tests/qcurl/testdata/http2/localhost.crt"));
            const QString keyPath = QDir(appDir).absoluteFilePath(
                QStringLiteral("../../tests/qcurl/testdata/http2/localhost.key"));
            if (!QFileInfo::exists(certPath) || !QFileInfo::exists(keyPath)) {
                m_skipReason = QStringLiteral(
                    "未找到本地 WSS 证书或私钥（tests/qcurl/testdata/http2/localhost.{crt,key}）。");
                return false;
            }
            args += {QStringLiteral("--tls"),
                     QStringLiteral("--cert"),
                     certPath,
                     QStringLiteral("--key"),
                     keyPath};
        }

        m_process.setProgram(QStringLiteral("node"));
        m_process.setArguments(args);
        m_process.setProcessChannelMode(QProcess::SeparateChannels);

        // 统一将工件写入 build/<...>/test-artifacts（由 appDir 推导，避免硬编码 build 目录名）。
        const QString artifactRoot = QDir(appDir).absoluteFilePath(
            QStringLiteral("../test-artifacts"));
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("QCURL_TEST_ARTIFACT_DIR"), artifactRoot);
        env.insert(QStringLiteral("QCURL_WEBSOCKET_EVIDENCE_DIR"), artifactRoot);
        m_process.setProcessEnvironment(env);

        m_process.start();

        if (!m_process.waitForStarted(2000)) {
            m_skipReason = QStringLiteral(
                               "无法启动本地 WebSocket 测试服务器（node）。请确认已安装 node。"
                               "%1")
                               .arg((m_kind == ServerKind::FragmentEcho)
                                        ? QStringLiteral(
                                              "（Fragment Echo 依赖 tests/qcurl/node_modules，可尝试 "
                                              "`cd tests/qcurl && npm ci`）")
                                        : QString());
            return false;
        }

        quint16 readyPort = 0;
        QString stdoutBuf;
        QString stderrBuf;
        QElapsedTimer timer;
        timer.start();

        while (timer.elapsed() < 5000) {
            if (m_process.state() == QProcess::NotRunning) {
                break;
            }

            m_process.waitForReadyRead(100);
            stdoutBuf += QString::fromUtf8(m_process.readAllStandardOutput());
            stderrBuf += QString::fromUtf8(m_process.readAllStandardError());

            QString artifactsPath;
            if (tryParseReady(stdoutBuf, readyPort, artifactsPath)) {
                if (!artifactsPath.isEmpty()) {
                    m_artifactsPath = artifactsPath;
                }
                break;
            }
        }

        if (readyPort == 0 || !waitForPortReady(readyPort, 3000)) {
            stop();
            m_skipReason = QStringLiteral("本地 WebSocket 测试服务器未在预期时间内就绪（未收到 "
                                          "READY marker 或端口不可达）。\n"
                                          "stdout:\n%1\nstderr:\n%2")
                               .arg(tailForLog(stdoutBuf), tailForLog(stderrBuf));
            return false;
        }

        m_port = readyPort;
        return true;
    }

    /// @brief 停止当前 server 进程。
    void stop()
    {
        if (m_process.state() == QProcess::NotRunning) {
            return;
        }

        m_process.terminate();
        if (!m_process.waitForFinished(1500)) {
            m_process.kill();
            m_process.waitForFinished(1500);
        }
    }

private:
    static bool tryParseReady(const QString &stdoutBuf, quint16 &outPort, QString &outArtifactsPath)
    {
        const QString marker    = QStringLiteral("QCURL_WEBSOCKET_TEST_SERVER_READY ");
        const QStringList lines = stdoutBuf.split('\n');
        for (const QString &rawLine : lines) {
            const QString line = rawLine.trimmed();
            if (!line.startsWith(marker)) {
                continue;
            }

            const QString jsonText = line.mid(marker.size()).trimmed();
            QJsonParseError parseError{};
            const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &parseError);
            if (!doc.isObject()) {
                continue;
            }
            const QJsonObject obj = doc.object();
            const int p           = obj.value(QStringLiteral("port")).toInt(0);
            if (p <= 0 || p > 65535) {
                continue;
            }
            outPort = static_cast<quint16>(p);

            const QString ap = obj.value(QStringLiteral("artifactsPath")).toString();
            if (!ap.isEmpty()) {
                outArtifactsPath = ap;
            }
            return true;
        }
        return false;
    }

    static bool waitForPortReady(quint16 port, int timeoutMs)
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

    static QString tailForLog(const QString &s)
    {
        constexpr int kMax = 4096;
        if (s.isEmpty()) {
            return QStringLiteral("(empty)");
        }
        if (s.size() <= kMax) {
            return s;
        }
        return QStringLiteral("...(truncated)...\n") + s.right(kMax);
    }

    Mode m_mode       = Mode::Ws;
    ServerKind m_kind = ServerKind::FragmentEcho;
    quint16 m_port    = 0;
    QString m_skipReason;
    QString m_artifactsPath;
    QProcess m_process;
};

} // namespace QCurl
QT_END_NAMESPACE

#endif // QCURL_TEST_QCWEBSOCKETTESTSERVER_H
