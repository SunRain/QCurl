/**
 * @file main.cpp
 * @brief 网络功能综合示例
 *
 * 演示 QCurl v2.17-v2.19 的新功能:
 * 1. HTTP/3 支持 (Core / Stable capability)
 * 2. WebSocket 有界异步收发 (Other Extras / Preview)
 * 3. 网络诊断工具 (Other Extras / Preview)
 *
 */

#include <QCoreApplication>
#include <QDebug>
#include <QFutureWatcher>
#include <QTextStream>
#include <QTimer>

#include <chrono>

// QCurl 核心头文件
#include "QCNetworkAccessManager.h"
#include "QCNetworkDiagnostics.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"

#include <curl/curl.h>

#ifdef QCURL_WEBSOCKET_SUPPORT
#include "QCWebSocket.h"
#endif

using namespace QCurl;

/**
 * @brief 网络功能演示类
 */
class NetworkFeaturesDemo : public QObject
{
    Q_OBJECT

public:
    NetworkFeaturesDemo(QObject *parent = nullptr)
        : QObject(parent)
    {
        m_manager = new QCNetworkAccessManager(this);
        m_output << "========================================\n";
        m_output << "QCurl 网络功能综合示例\n";
        m_output << "v2.17-v2.19 新功能演示\n";
        m_output << "========================================\n\n";
    }

public Q_SLOTS:
    /**
     * @brief 运行所有示例
     */
    void runAllDemos()
    {
        m_output << "【演示菜单】\n";
        m_output << "1. HTTP/3 请求示例\n";
        m_output << "2. WebSocket Preview 收发示例\n";
        m_output << "3. 网络诊断示例\n";
        m_output << "4. 综合演示（依次执行上述所有示例）\n\n";

        // 开始演示
        demoHttp3();
    }

private Q_SLOTS:
    /**
     * @brief HTTP/3 请求示例
     */
    void demoHttp3()
    {
        m_output << "========================================\n";
        m_output << "1. HTTP/3 请求示例\n";
        m_output << "========================================\n\n";

        // 检查 HTTP/3 支持
        curl_version_info_data *ver = curl_version_info(CURLVERSION_NOW);
        m_output << "libcurl 版本: " << ver->version << "\n";

#ifdef CURL_VERSION_HTTP3
        bool http3Supported = (ver->features & CURL_VERSION_HTTP3) != 0;
        m_output << "HTTP/3 支持: " << (http3Supported ? "✅ 是" : "❌ 否") << "\n\n";
#else
        m_output << "HTTP/3 支持: ❌ 否（编译时未启用）\n\n";
#endif

        // 创建 HTTP/3 请求
        QUrl url("https://www.cloudflare.com"); // Cloudflare 支持 HTTP/3
        QCNetworkRequest request(url);

        // 尝试使用 HTTP/3（如果不支持会自动降级）
        request.setHttpVersion(QCNetworkHttpVersion::Http3);

        m_output << "发送 HTTP/3 请求到: " << url.toString() << "\n";
        m_output << "HTTP 版本: Http3（尝试 HTTP/3，失败则降级）\n";

        QCNetworkReply *reply = m_manager->get(request);

        connect(reply, &QCNetworkReply::finished, this, [this, reply]() {
            if (reply->error() == NetworkError::NoError) {
                m_output << "\n✅ HTTP/3 请求成功!\n";
                int statusCode                      = -1;
                const QList<QByteArray> headerLines = reply->rawHeaderData().split('\n');
                for (const QByteArray &line : headerLines) {
                    const QByteArray trimmedLine = line.trimmed();
                    if (!trimmedLine.startsWith("HTTP/")) {
                        continue;
                    }

                    const QList<QByteArray> parts = trimmedLine.split(' ');
                    if (parts.size() < 2) {
                        continue;
                    }

                    bool ok        = false;
                    const int code = parts.at(1).toInt(&ok);
                    if (ok) {
                        statusCode = code;
                    }
                }

                if (statusCode > 0) {
                    m_output << "状态码: " << statusCode << "\n";
                } else {
                    m_output << "状态码: （未获取到）\n";
                }

                m_output << "响应大小: " << reply->bytesReceived() << " 字节\n";

                // 尝试获取实际使用的 HTTP 版本
                QString altSvc;
                const QList<RawHeaderPair> headers = reply->rawHeaders();
                for (const auto &header : headers) {
                    if (QString::fromUtf8(header.first)
                            .compare(QStringLiteral("Alt-Svc"), Qt::CaseInsensitive)
                        != 0) {
                        continue;
                    }

                    altSvc = QString::fromUtf8(header.second);
                    break;
                }

                if (!altSvc.isEmpty()) {
                    m_output << "Alt-Svc 头: " << altSvc << "\n";
                }
            } else {
                m_output << "\n❌ HTTP/3 请求失败: " << reply->errorString() << "\n";
            }
            m_output << "\n";
            reply->deleteLater();

            // 继续下一个演示
            demoWebSocketPreview();
        });
    }

    /**
     * @brief WebSocket Preview 收发示例
     */
    void demoWebSocketPreview()
    {
        m_output << "========================================\n";
        m_output << "2. WebSocket Preview 收发示例\n";
        m_output << "========================================\n\n";

#ifdef QCURL_WEBSOCKET_SUPPORT
        m_output << "WebSocket 支持: ✅ 已启用\n\n";

        QUrl wsUrl("wss://echo.websocket.org");
        QCWebSocketOptions options;
        QString optionError;
        if (!options.setMaxMessageBytes(8 * 1024 * 1024, &optionError)
            || !options.setMaxPendingSendBytes(4 * 1024 * 1024, &optionError)
            || !options.setCloseHandshakeTimeout(std::chrono::seconds{5}, &optionError)) {
            m_output << "❌ WebSocket 配置无效: " << optionError << "\n\n";
            demoNetworkDiagnostics();
            return;
        }
        QCWebSocket *socket = new QCWebSocket(wsUrl, options, this);

        m_output << "连接到: " << wsUrl.toString() << "\n";
        m_output << "  - 最大消息: " << options.maxMessageBytes() << " 字节\n";
        m_output << "  - 待发送队列: " << options.maxPendingSendBytes() << " 字节\n";
        m_output << "  - 关闭握手超时: " << options.closeHandshakeTimeout().count() << " ms\n\n";

        connect(socket, &QCWebSocket::connected, this, [this, socket]() {
            m_output << "✅ WebSocket 连接成功!\n";
            const QString testMessage = QStringLiteral("QCurl WebSocket Preview echo");
            m_output << "发送测试消息 (大小: " << testMessage.toUtf8().size() << " 字节)...\n";
            static_cast<void>(socket->sendTextMessage(testMessage));
        });

        connect(socket, &QCWebSocket::textMessageReceived, this, [this, socket](const QString &msg) {
            m_output << "接收到回显消息 (大小: " << msg.toUtf8().size() << " 字节)\n";
            m_output << "\n";
            static_cast<void>(socket->close());
        });

        connect(socket, &QCWebSocket::disconnected, this, [this, socket]() {
            m_output << "WebSocket 已断开连接\n\n";
            socket->deleteLater();

            // 继续下一个演示
            demoNetworkDiagnostics();
        });

        connect(socket, &QCWebSocket::errorOccurred, this, [this, socket](const QString &error) {
            m_output << "❌ WebSocket 错误: " << error << "\n\n";
            socket->deleteLater();

            // 即使失败也继续下一个演示
            demoNetworkDiagnostics();
        });

        static_cast<void>(socket->open());
#else
        m_output << "WebSocket 支持: ❌ 未启用\n";
        m_output << "（编译时未定义 QCURL_WEBSOCKET_SUPPORT）\n\n";

        // 继续下一个演示
        demoNetworkDiagnostics();
#endif
    }

    /**
     * @brief 网络诊断示例
     */
    void demoNetworkDiagnostics()
    {
        m_output << "========================================\n";
        m_output << "3. 网络诊断示例\n";
        m_output << "========================================\n\n";

        QCNetworkDiagnosticsOptions quickOptions;
        QString optionError;
        if (!quickOptions.setTimeout(std::chrono::milliseconds{5000}, &optionError)) {
            m_output << "❌ DNS 诊断配置无效: " << optionError << "\n";
            return;
        }

        QCNetworkDiagnosticsOptions httpOptions;
        if (!httpOptions.setTimeout(std::chrono::milliseconds{10000}, &optionError)) {
            m_output << "❌ HTTP 诊断配置无效: " << optionError << "\n";
            return;
        }

        QCNetworkDiagnosticsOptions httpPortOptions = quickOptions;
        if (!httpPortOptions.setPort(80, &optionError)) {
            m_output << "❌ TCP 诊断配置无效: " << optionError << "\n";
            return;
        }

        QCNetworkDiagnosticsOptions sslOptions = httpOptions;
        if (!sslOptions.setPort(443, &optionError)) {
            m_output << "❌ SSL 诊断配置无效: " << optionError << "\n";
            return;
        }

        reportDiagnostic(QStringLiteral("DNS 解析"),
                         QCNetworkDiagnostics::resolveDNS("example.com", quickOptions));
        reportDiagnostic(QStringLiteral("TCP 连接测试"),
                         QCNetworkDiagnostics::testConnection("example.com", httpPortOptions));
        reportDiagnostic(QStringLiteral("SSL 证书检查"),
                         QCNetworkDiagnostics::checkSSL("www.github.com", sslOptions));
        reportDiagnostic(QStringLiteral("HTTP 探测"),
                         QCNetworkDiagnostics::probeHTTP(QUrl("https://www.google.com"),
                                                         httpOptions));
        reportDiagnostic(QStringLiteral("综合诊断"),
                         QCNetworkDiagnostics::diagnose(QUrl("https://www.cloudflare.com"),
                                                        httpOptions));
    }

    void reportDiagnostic(const QString &label, QFuture<DiagResult> future)
    {
        ++m_pendingDiagnostics;
        auto *watcher = new QFutureWatcher<DiagResult>(this);
        connect(watcher, &QFutureWatcher<DiagResult>::finished, this, [this, watcher, label]() {
            m_output << "【" << label << "】\n";
            if (watcher->future().resultCount() == 1) {
                m_output << watcher->future().result().toString() << "\n";
            } else {
                m_output << "诊断被取消且没有结果\n";
            }
            watcher->deleteLater();

            if (--m_pendingDiagnostics == 0) {
                m_output << "\n========================================\n";
                m_output << "所有演示完成!\n";
                m_output << "========================================\n";
                QTimer::singleShot(0, qApp, &QCoreApplication::quit);
            }
        });
        watcher->setFuture(future);
    }

private:
    QCNetworkAccessManager *m_manager;
    QTextStream m_output{stdout};
    int m_pendingDiagnostics = 0;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    NetworkFeaturesDemo demo;

    // 延迟启动演示，确保事件循环已运行
    QTimer::singleShot(0, &demo, &NetworkFeaturesDemo::runAllDemos);

    return app.exec();
}

#include "main.moc"
