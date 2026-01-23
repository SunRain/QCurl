/**
 * @file main.cpp
 * @brief 代理配置演示程序
 * 
 * 演示各种代理配置：
 * - HTTP/HTTPS 代理
 * - SOCKS5 代理
 * - 代理认证
 * - 代理与 SSL 结合
 * 
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkSslConfig.h"

#include <QCoreApplication>
#include <QTimer>

#include <iostream>

using namespace QCurl;

class ProxyDemo : public QObject
{
    Q_OBJECT

public:
    explicit ProxyDemo(QObject *parent = nullptr)
        : QObject(parent)
    {
        manager = new QCNetworkAccessManager(this);
    }

    void demo1_NoProxy()
    {
        std::cout << "\n=== 演示 1：无代理（直连）===" << std::endl;

        QCNetworkRequest request(QUrl("https://httpbin.org/get"));
        makeRequest(request);
    }

    void demo2_HttpProxy()
    {
        std::cout << "\n=== 演示 2：HTTP 代理（无认证）===" << std::endl;

        QCNetworkProxyConfig proxyConfig;
        proxyConfig.type     = QCNetworkProxyConfig::ProxyType::Http;
        proxyConfig.hostName = "proxy.example.com";
        proxyConfig.port     = 8080;

        QCNetworkRequest request(QUrl("http://httpbin.org/get"));
        request.setProxyConfig(proxyConfig);

        std::cout << "代理配置：" << std::endl;
        std::cout << "  类型: HTTP" << std::endl;
        std::cout << "  地址: " << proxyConfig.hostName.toStdString() << ":" << proxyConfig.port
                  << std::endl;

        makeRequest(request);
    }

    void demo3_HttpProxyAuth()
    {
        std::cout << "\n=== 演示 3：HTTP 代理（带认证）===" << std::endl;

        QCNetworkProxyConfig proxyConfig;
        proxyConfig.type     = QCNetworkProxyConfig::ProxyType::Http;
        proxyConfig.hostName = "auth-proxy.example.com";
        proxyConfig.port     = 8080;
        proxyConfig.userName = "username";
        proxyConfig.password = "password";

        QCNetworkRequest request(QUrl("http://httpbin.org/get"));
        request.setProxyConfig(proxyConfig);

        std::cout << "代理配置：" << std::endl;
        std::cout << "  类型: HTTP（认证）" << std::endl;
        std::cout << "  地址: " << proxyConfig.hostName.toStdString() << ":" << proxyConfig.port
                  << std::endl;
        std::cout << "  用户名: " << proxyConfig.userName.toStdString() << std::endl;
        std::cout << "  密码: " << QString(proxyConfig.password.length(), '*').toStdString()
                  << std::endl;

        makeRequest(request);
    }

    void demo4_HttpsProxy()
    {
        std::cout << "\n=== 演示 4：HTTPS 代理 ===" << std::endl;

        QCNetworkProxyConfig proxyConfig;
        proxyConfig.type     = QCNetworkProxyConfig::ProxyType::Https;
        proxyConfig.hostName = "secure-proxy.example.com";
        proxyConfig.port     = 443;

        QCNetworkRequest request(QUrl("https://httpbin.org/get"));
        request.setProxyConfig(proxyConfig);

        std::cout << "代理配置：" << std::endl;
        std::cout << "  类型: HTTPS（加密代理）" << std::endl;
        std::cout << "  地址: " << proxyConfig.hostName.toStdString() << ":" << proxyConfig.port
                  << std::endl;

        makeRequest(request);
    }

    void demo5_Socks5Proxy()
    {
        std::cout << "\n=== 演示 5：SOCKS5 代理（无认证）===" << std::endl;

        QCNetworkProxyConfig proxyConfig;
        proxyConfig.type     = QCNetworkProxyConfig::ProxyType::Socks5;
        proxyConfig.hostName = "socks5.example.com";
        proxyConfig.port     = 1080;

        QCNetworkRequest request(QUrl("https://httpbin.org/get"));
        request.setProxyConfig(proxyConfig);

        std::cout << "代理配置：" << std::endl;
        std::cout << "  类型: SOCKS5" << std::endl;
        std::cout << "  地址: " << proxyConfig.hostName.toStdString() << ":" << proxyConfig.port
                  << std::endl;

        makeRequest(request);
    }

    void demo6_Socks5ProxyAuth()
    {
        std::cout << "\n=== 演示 6：SOCKS5 代理（带认证）===" << std::endl;

        QCNetworkProxyConfig proxyConfig;
        proxyConfig.type     = QCNetworkProxyConfig::ProxyType::Socks5;
        proxyConfig.hostName = "socks5-auth.example.com";
        proxyConfig.port     = 1080;
        proxyConfig.userName = "socks_user";
        proxyConfig.password = "socks_pass";

        QCNetworkRequest request(QUrl("https://httpbin.org/get"));
        request.setProxyConfig(proxyConfig);

        std::cout << "代理配置：" << std::endl;
        std::cout << "  类型: SOCKS5（认证）" << std::endl;
        std::cout << "  地址: " << proxyConfig.hostName.toStdString() << ":" << proxyConfig.port
                  << std::endl;
        std::cout << "  用户名: " << proxyConfig.userName.toStdString() << std::endl;
        std::cout << "  密码: " << QString(proxyConfig.password.length(), '*').toStdString()
                  << std::endl;

        makeRequest(request);
    }

    void demo7_ProxyWithSsl()
    {
        std::cout << "\n=== 演示 7：代理 + SSL 证书验证 ===" << std::endl;

        QCNetworkProxyConfig proxyConfig;
        proxyConfig.type     = QCNetworkProxyConfig::ProxyType::Http;
        proxyConfig.hostName = "proxy.example.com";
        proxyConfig.port     = 8080;

        QCNetworkSslConfig sslConfig = QCNetworkSslConfig::defaultConfig();
        sslConfig.verifyPeer         = true;
        sslConfig.verifyHost         = true;

        QCNetworkRequest request(QUrl("https://httpbin.org/get"));
        request.setProxyConfig(proxyConfig);
        request.setSslConfig(sslConfig);

        std::cout << "代理配置：" << std::endl;
        std::cout << "  类型: HTTP" << std::endl;
        std::cout << "  地址: " << proxyConfig.hostName.toStdString() << ":" << proxyConfig.port
                  << std::endl;
        std::cout << "\nSSL 配置：" << std::endl;
        std::cout << "  验证对等证书: " << (sslConfig.verifyPeer ? "是" : "否") << std::endl;
        std::cout << "  验证主机名: " << (sslConfig.verifyHost ? "是" : "否") << std::endl;

        makeRequest(request);
    }

signals:
    void demoComplete();

private:
    void makeRequest(const QCNetworkRequest &request)
    {
        std::cout << "\n发送请求到: " << request.url().toString().toStdString() << std::endl;
        std::cout << "等待响应...\n" << std::endl;

        auto *reply = manager->sendGet(request);

        connect(reply, &QCNetworkReply::finished, [this, reply]() {
            if (reply->error() == NetworkError::NoError) {
                std::cout << "✅ 请求成功！" << std::endl;

                if (auto data = reply->readAll()) {
                    std::cout << "响应大小: " << data->size() << " 字节" << std::endl;

                    // 显示部分响应（前 200 字符）
                    QString response = QString::fromUtf8(*data);
                    if (response.length() > 200) {
                        std::cout << "响应预览（前 200 字符）:\n"
                                  << response.left(200).toStdString() << "..." << std::endl;
                    } else {
                        std::cout << "响应内容:\n" << response.toStdString() << std::endl;
                    }
                }
            } else {
                std::cout << "❌ 请求失败！" << std::endl;
                std::cout << "错误码: " << static_cast<int>(reply->error()) << std::endl;
                std::cout << "错误信息: " << reply->errorString().toStdString() << std::endl;

                // 提示常见原因
                if (reply->error() == NetworkError::ConnectionRefused
                    || reply->error() == NetworkError::ConnectionTimeout
                    || reply->error() == NetworkError::HostNotFound) {
                    std::cout << "\n💡 提示：这是预期行为（使用示例代理地址）" << std::endl;
                    std::cout << "   实际使用时请替换为真实的代理服务器地址" << std::endl;
                }
            }

            reply->deleteLater();
            emit demoComplete();
        });
    }

private:
    QCNetworkAccessManager *manager = nullptr;
};

void showMenu()
{
    std::cout << "\n========================================" << std::endl;
    std::cout << "QCurl 代理配置演示程序 v2.2.1" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "\n请选择演示：" << std::endl;
    std::cout << "  1 - 无代理（直连）" << std::endl;
    std::cout << "  2 - HTTP 代理（无认证）" << std::endl;
    std::cout << "  3 - HTTP 代理（带认证）" << std::endl;
    std::cout << "  4 - HTTPS 代理" << std::endl;
    std::cout << "  5 - SOCKS5 代理（无认证）" << std::endl;
    std::cout << "  6 - SOCKS5 代理（带认证）" << std::endl;
    std::cout << "  7 - 代理 + SSL 证书验证" << std::endl;
    std::cout << "  0 - 退出" << std::endl;
    std::cout << "\n用法: ./ProxyDemo [演示编号]" << std::endl;
    std::cout << "示例: ./ProxyDemo 1" << std::endl;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("ProxyDemo");
    app.setApplicationVersion("2.2.1");

    ProxyDemo demo;

    // 解析命令行参数
    int demoNum = 0;
    if (argc > 1) {
        demoNum = QString(argv[1]).toInt();
    }

    if (demoNum == 0) {
        showMenu();
        return 0;
    }

    QObject::connect(&demo, &ProxyDemo::demoComplete, &app, &QCoreApplication::quit);

    switch (demoNum) {
        case 1:
            QTimer::singleShot(0, &demo, &ProxyDemo::demo1_NoProxy);
            break;
        case 2:
            QTimer::singleShot(0, &demo, &ProxyDemo::demo2_HttpProxy);
            break;
        case 3:
            QTimer::singleShot(0, &demo, &ProxyDemo::demo3_HttpProxyAuth);
            break;
        case 4:
            QTimer::singleShot(0, &demo, &ProxyDemo::demo4_HttpsProxy);
            break;
        case 5:
            QTimer::singleShot(0, &demo, &ProxyDemo::demo5_Socks5Proxy);
            break;
        case 6:
            QTimer::singleShot(0, &demo, &ProxyDemo::demo6_Socks5ProxyAuth);
            break;
        case 7:
            QTimer::singleShot(0, &demo, &ProxyDemo::demo7_ProxyWithSsl);
            break;
        default:
            std::cerr << "错误：无效的演示编号 " << demoNum << std::endl;
            return 1;
    }

    return app.exec();
}

#include "main.moc"
