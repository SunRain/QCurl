/*
 * QtTest WebSocket 本地测试服务器 helper。
 *
 * 封装 node tests/websocket-fragment-server.js 的启动/停止、READY marker 解析与端口就绪探测，
 * 支持 ws/wss 两种模式并提供 CA 证书路径，供多个 WebSocket 测试复用。
 */

#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHostAddress>
#include <QProcess>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QThread>

QT_BEGIN_NAMESPACE
namespace QCurl {

class QCWebSocketTestServer
{
public:
    enum class Mode {
        Ws,
        Wss,
    };

    QCWebSocketTestServer() = default;
    ~QCWebSocketTestServer() { stop(); }

    QCWebSocketTestServer(const QCWebSocketTestServer &)            = delete;
    QCWebSocketTestServer &operator=(const QCWebSocketTestServer &) = delete;

    [[nodiscard]] QString skipReason() const { return m_skipReason; }
    [[nodiscard]] quint16 port() const { return m_port; }

    [[nodiscard]] QString baseUrl() const
    {
        if (m_port == 0) {
            return {};
        }
        const QString scheme = (m_mode == Mode::Wss) ? QStringLiteral("wss") : QStringLiteral("ws");
        return QStringLiteral("%1://localhost:%2").arg(scheme).arg(m_port);
    }

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

    [[nodiscard]] QString caCertPath() const
    {
        const QString appDir = QCoreApplication::applicationDirPath();
        return QDir(appDir).absoluteFilePath(QStringLiteral("../../tests/testdata/http2/localhost.crt"));
    }

    bool start(Mode mode)
    {
        stop();

        m_mode       = mode;
        m_port       = 0;
        m_skipReason = {};

        const QString appDir     = QCoreApplication::applicationDirPath();
        const QString scriptPath = QDir(appDir).absoluteFilePath(QStringLiteral("../../tests/websocket-fragment-server.js"));
        if (!QFileInfo::exists(scriptPath)) {
            m_skipReason = QStringLiteral("未找到本地 WebSocket 测试服务器脚本（tests/websocket-fragment-server.js）。");
            return false;
        }

        QStringList args{scriptPath, QStringLiteral("--port"), QStringLiteral("0")};
        if (m_mode == Mode::Wss) {
            const QString certPath = QDir(appDir).absoluteFilePath(QStringLiteral("../../tests/testdata/http2/localhost.crt"));
            const QString keyPath  = QDir(appDir).absoluteFilePath(QStringLiteral("../../tests/testdata/http2/localhost.key"));
            if (!QFileInfo::exists(certPath) || !QFileInfo::exists(keyPath)) {
                m_skipReason = QStringLiteral(
                    "未找到本地 WSS 证书或私钥（tests/testdata/http2/localhost.{crt,key}）。");
                return false;
            }
            args += {QStringLiteral("--tls"), QStringLiteral("--cert"), certPath, QStringLiteral("--key"), keyPath};
        }

        m_process.setProgram(QStringLiteral("node"));
        m_process.setArguments(args);
        m_process.setProcessChannelMode(QProcess::SeparateChannels);
        m_process.start();

        if (!m_process.waitForStarted(2000)) {
            m_skipReason = QStringLiteral("无法启动本地 WebSocket 测试服务器（node）。请确认已安装 node，且 tests/node_modules 依赖可用。");
            return false;
        }

        quint16 readyPort = 0;
        QString stdoutBuf;
        QString stderrBuf;
        QElapsedTimer timer;
        timer.start();

        static const QRegularExpression readyRe(
            QStringLiteral(R"(QCURL_WEBSOCKET_TEST_SERVER_READY\s+\{"port":(\d+)\})"));

        while (timer.elapsed() < 5000) {
            if (m_process.state() == QProcess::NotRunning) {
                break;
            }

            m_process.waitForReadyRead(100);
            stdoutBuf += QString::fromUtf8(m_process.readAllStandardOutput());
            stderrBuf += QString::fromUtf8(m_process.readAllStandardError());

            const QRegularExpressionMatch m = readyRe.match(stdoutBuf);
            if (m.hasMatch()) {
                bool ok = false;
                const int p = m.captured(1).toInt(&ok);
                if (ok && p > 0 && p <= 65535) {
                    readyPort = static_cast<quint16>(p);
                    break;
                }
            }
        }

        if (readyPort == 0 || !waitForPortReady(readyPort, 3000)) {
            stop();
            m_skipReason = QStringLiteral(
                "本地 WebSocket 测试服务器未在预期时间内就绪（未收到 READY marker 或端口不可达）。\n"
                "stdout:\n%1\nstderr:\n%2")
                               .arg(tailForLog(stdoutBuf), tailForLog(stderrBuf));
            return false;
        }

        m_port = readyPort;
        return true;
    }

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

    Mode m_mode = Mode::Ws;
    quint16 m_port = 0;
    QString m_skipReason;
    QProcess m_process;
};

} // namespace QCurl
QT_END_NAMESPACE

