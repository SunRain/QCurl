/**
 * @file main.cpp
 * @brief 网络功能综合示例
 *
 * 演示 QCurl v2.17-v2.19 的新功能:
 * 1. HTTP/3 支持 (v2.17.0)
 * 2. WebSocket 压缩扩展 (v2.18.0)
 * 3. 网络诊断工具 (v2.19.0)
 *
 */

#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>
#include <QDebug>

// QCurl 核心头文件
#include "QCNetworkAccessManager.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkDiagnostics.h"

#ifdef QCURL_WEBSOCKET_SUPPORT
#include "QCWebSocket.h"
#include "QCWebSocketCompressionConfig.h"
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

public slots:
    /**
     * @brief 运行所有示例
     */
    void runAllDemos()
    {
        m_output << "【演示菜单】\n";
        m_output << "1. HTTP/3 请求示例\n";
        m_output << "2. WebSocket 压缩示例\n";
        m_output << "3. 网络诊断示例\n";
        m_output << "4. 综合演示（依次执行上述所有示例）\n\n";

        // 开始演示
        demoHttp3();
    }

private slots:
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
        QUrl url("https://www.cloudflare.com");  // Cloudflare 支持 HTTP/3
        QCNetworkRequest request(url);

        // 尝试使用 HTTP/3（如果不支持会自动降级）
        request.setHttpVersion(QCNetworkHttpVersion::Http3);

        m_output << "发送 HTTP/3 请求到: " << url.toString() << "\n";
        m_output << "HTTP 版本: Http3（尝试 HTTP/3，失败则降级）\n";

        QCNetworkReply *reply = m_manager->get(request);

        connect(reply, &QCNetworkReply::finished, this, [this, reply]() {
            if (reply->error() == NetworkError::NoError) {
                m_output << "\n✅ HTTP/3 请求成功!\n";
                m_output << "状态码: " << reply->statusCode() << "\n";
                m_output << "响应大小: " << reply->bytesAvailable() << " 字节\n";

                // 尝试获取实际使用的 HTTP 版本
                QString protocol = reply->rawHeader("Alt-Svc");
                if (!protocol.isEmpty()) {
                    m_output << "Alt-Svc 头: " << protocol << "\n";
                }
            } else {
                m_output << "\n❌ HTTP/3 请求失败: " << reply->errorString() << "\n";
            }
            m_output << "\n";
            reply->deleteLater();

            // 继续下一个演示
            demoWebSocketCompression();
        });

        reply->execute();
    }

    /**
     * @brief WebSocket 压缩示例
     */
    void demoWebSocketCompression()
    {
        m_output << "========================================\n";
        m_output << "2. WebSocket 压缩示例\n";
        m_output << "========================================\n\n";

#ifdef QCURL_WEBSOCKET_SUPPORT
        m_output << "WebSocket 支持: ✅ 已启用\n\n";

        // 创建 WebSocket 连接
        QUrl wsUrl("wss://echo.websocket.org");
        QCWebSocket *socket = new QCWebSocket(wsUrl, this);

        // 配置压缩（RFC 7692 permessage-deflate）
        QCWebSocketCompressionConfig compConfig = QCWebSocketCompressionConfig::defaultConfig();
        socket->setCompressionConfig(compConfig);

        m_output << "连接到: " << wsUrl.toString() << "\n";
        m_output << "压缩配置:\n";
        m_output << "  - 启用: " << (compConfig.enabled ? "是" : "否") << "\n";
        m_output << "  - 客户端窗口位数: " << compConfig.clientMaxWindowBits << " (32KB)\n";
        m_output << "  - 压缩级别: " << compConfig.compressionLevel << "\n";
        m_output << "  - 扩展头: " << compConfig.toExtensionHeader() << "\n\n";

        connect(socket, &QCWebSocket::connected, this, [this, socket]() {
            m_output << "✅ WebSocket 连接成功!\n";

            if (socket->isCompressionNegotiated()) {
                m_output << "✅ 压缩协商成功!\n";
                m_output << "协商后的配置: " << socket->compressionConfig().toExtensionHeader() << "\n";
            } else {
                m_output << "ℹ️  服务器不支持压缩或拒绝了压缩请求\n";
            }

            // 发送测试消息
            QString testMessage = QString("测试消息 - ").repeated(50);  // 重复内容以测试压缩效果
            m_output << "\n发送测试消息 (原始大小: " << testMessage.toUtf8().size() << " 字节)...\n";
            socket->sendTextMessage(testMessage);
        });

        connect(socket, &QCWebSocket::textMessageReceived, this, [this, socket](const QString &msg) {
            m_output << "接收到回显消息 (大小: " << msg.toUtf8().size() << " 字节)\n";

            if (socket->isCompressionNegotiated()) {
                m_output << "\n压缩统计信息:\n";
                m_output << socket->compressionStats() << "\n";
            }

            m_output << "\n";
            socket->close();
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

        socket->open();
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

        // 1. DNS 解析示例
        m_output << "【DNS 解析】\n";
        auto dnsResult = QCNetworkDiagnostics::resolveDNS("example.com", 5000);
        m_output << dnsResult.toString() << "\n";

        // 2. TCP 连接测试示例
        m_output << "【TCP 连接测试】\n";
        auto connResult = QCNetworkDiagnostics::testConnection("example.com", 80, 5000);
        m_output << connResult.toString() << "\n";

        // 3. SSL 证书检查示例
        m_output << "【SSL 证书检查】\n";
        auto sslResult = QCNetworkDiagnostics::checkSSL("www.github.com", 443, 10000);
        m_output << sslResult.toString() << "\n";

        // 4. HTTP 探测示例
        m_output << "【HTTP 探测】\n";
        auto httpResult = QCNetworkDiagnostics::probeHTTP(QUrl("https://www.google.com"), 10000);
        m_output << httpResult.toString() << "\n";

        // 5. 综合诊断示例
        m_output << "【综合诊断】\n";
        auto diagResult = QCNetworkDiagnostics::diagnose(QUrl("https://www.cloudflare.com"));
        m_output << diagResult.toString() << "\n";

        m_output << "\n========================================\n";
        m_output << "所有演示完成!\n";
        m_output << "========================================\n";

        // 退出应用
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    }

private:
    QCNetworkAccessManager *m_manager;
    QTextStream m_output{stdout};
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
