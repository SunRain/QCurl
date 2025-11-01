/**
 * @file WebSocketCompressionDemo/main.cpp
 * @brief WebSocket 压缩扩展演示程序
 * 
 * 演示 RFC 7692 permessage-deflate 压缩的使用和效果。
 * 
 */

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include "QCWebSocket.h"
#include "QCWebSocketCompressionConfig.h"

using namespace QCurl;

void printSeparator(const QString &title) {
    qDebug() << "\n" << QString(60, '=');
    qDebug() << title;
    qDebug() << QString(60, '=') << "\n";
}

/**
 * @brief 演示 1: 启用压缩 vs 禁用压缩
 */
void demo1_CompressionComparison()
{
    printSeparator("演示 1: 压缩效果对比");
    
    // Echo 服务器（需要支持压缩）
    QUrl url("wss://echo.websocket.org");
    
    // 测试消息（重复文本，压缩效果明显）
    QString testMessage = QString("Hello World! ").repeated(100);  // 1300 字节
    
    qDebug() << "测试消息大小:" << testMessage.toUtf8().size() << "字节";
    qDebug() << "测试服务器:" << url.toString();
    
    // 1. 禁用压缩
    qDebug() << "\n--- 禁用压缩 ---";
    QCWebSocket socket1(url);
    
    QObject::connect(&socket1, &QCWebSocket::connected, [&]() {
        qDebug() << "✅ 已连接（无压缩）";
        qDebug() << "压缩协商:" << (socket1.isCompressionNegotiated() ? "是" : "否");
        socket1.sendTextMessage(testMessage);
    });
    
    QObject::connect(&socket1, &QCWebSocket::textMessageReceived, [&](const QString &msg) {
        qDebug() << "收到回显:" << msg.size() << "字节";
        qDebug() << "统计:" << socket1.compressionStats();
        socket1.close();
    });
    
    socket1.open();
    
    // 等待完成
    QTimer::singleShot(3000, [&]() {
        // 2. 启用压缩
        qDebug() << "\n--- 启用压缩 ---";
        QCWebSocket socket2(url);
        
        // 配置压缩
        socket2.setCompressionConfig(QCWebSocketCompressionConfig::defaultConfig());
        
        QObject::connect(&socket2, &QCWebSocket::connected, [&]() {
            qDebug() << "✅ 已连接（启用压缩）";
            qDebug() << "压缩协商:" << (socket2.isCompressionNegotiated() ? "是" : "否");
            socket2.sendTextMessage(testMessage);
        });
        
        QObject::connect(&socket2, &QCWebSocket::textMessageReceived, [&](const QString &msg) {
            qDebug() << "收到回显:" << msg.size() << "字节";
            qDebug() << "统计:" << socket2.compressionStats();
            socket2.close();
            
            // 演示完成
            QTimer::singleShot(1000, []() {
                qApp->quit();
            });
        });
        
        socket2.open();
    });
}

/**
 * @brief 演示 2: 不同压缩配置的效果
 */
void demo2_CompressionPresets()
{
    printSeparator("演示 2: 不同压缩配置");
    
    QUrl url("wss://echo.websocket.org");
    QString testMessage = QString("The quick brown fox jumps over the lazy dog. ").repeated(50);
    
    qDebug() << "测试消息:" << testMessage.toUtf8().size() << "字节";
    
    auto configs = QList<QPair<QString, QCWebSocketCompressionConfig>>{
        {"默认配置", QCWebSocketCompressionConfig::defaultConfig()},
        {"低内存配置", QCWebSocketCompressionConfig::lowMemoryConfig()},
        {"最大压缩配置", QCWebSocketCompressionConfig::maxCompressionConfig()}
    };
    
    int currentIndex = 0;
    
    std::function<void()> testNextConfig;
    testNextConfig = [&, url, testMessage]() mutable {
        if (currentIndex >= configs.size()) {
            qDebug() << "\n所有配置测试完成";
            QTimer::singleShot(1000, []() { qApp->quit(); });
            return;
        }
        
        auto [name, config] = configs[currentIndex++];
        qDebug() << "\n--- 测试:" << name << "---";
        qDebug() << "  窗口大小:" << config.clientMaxWindowBits;
        qDebug() << "  压缩级别:" << config.compressionLevel;
        qDebug() << "  无上下文接管:" << (config.clientNoContextTakeover ? "是" : "否");
        
        auto *socket = new QCWebSocket(url);
        socket->setCompressionConfig(config);
        
        QObject::connect(socket, &QCWebSocket::connected, [socket, testMessage]() {
            qDebug() << "✅ 已连接";
            qDebug() << "压缩协商:" << (socket->isCompressionNegotiated() ? "是" : "否");
            socket->sendTextMessage(testMessage);
        });
        
        QObject::connect(socket, &QCWebSocket::textMessageReceived, [socket, &testNextConfig](const QString &) {
            qDebug() << "压缩统计:\n" << socket->compressionStats();
            socket->close();
            socket->deleteLater();
            
            // 测试下一个配置
            QTimer::singleShot(1000, testNextConfig);
        });
        
        socket->open();
    };
    
    testNextConfig();
}

/**
 * @brief 演示 3: 大消息压缩效果
 */
void demo3_LargeMessageCompression()
{
    printSeparator("演示 3: 大消息压缩效果");
    
    QUrl url("wss://echo.websocket.org");
    
    // 生成不同大小的消息
    auto testSizes = {1024, 10240, 102400};  // 1KB, 10KB, 100KB
    
    qDebug() << "测试不同大小消息的压缩效果";
    
    int currentIndex = 0;
    QVector<int> sizes(testSizes.begin(), testSizes.end());
    
    std::function<void()> testNextSize;
    testNextSize = [&, url]() mutable {
        if (currentIndex >= sizes.size()) {
            qDebug() << "\n大消息测试完成";
            QTimer::singleShot(1000, []() { qApp->quit(); });
            return;
        }
        
        int size = sizes[currentIndex++];
        QString message = QString("A").repeated(size);
        
        qDebug() << "\n--- 测试:" << size << "字节消息 ---";
        
        auto *socket = new QCWebSocket(url);
        socket->setCompressionConfig(QCWebSocketCompressionConfig::defaultConfig());
        
        QObject::connect(socket, &QCWebSocket::connected, [socket, message]() {
            qDebug() << "✅ 已连接，发送消息...";
            socket->sendTextMessage(message);
        });
        
        QObject::connect(socket, &QCWebSocket::textMessageReceived, [socket, &testNextSize](const QString &msg) {
            qDebug() << "收到回显:" << msg.size() << "字节";
            qDebug() << socket->compressionStats();
            socket->close();
            socket->deleteLater();
            
            QTimer::singleShot(1000, testNextSize);
        });
        
        socket->open();
    };
    
    testNextSize();
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    qDebug() << "QCurl WebSocket 压缩演示";
    qDebug() << "版本: 2.18.0";
    qDebug() << "\n选择演示:";
    qDebug() << "  1 - 压缩效果对比（启用 vs 禁用）";
    qDebug() << "  2 - 不同压缩配置效果";
    qDebug() << "  3 - 大消息压缩效果";
    
    int demo = 1;
    if (argc > 1) {
        demo = QString(argv[1]).toInt();
    }
    
    qDebug() << "\n运行演示" << demo << "...";
    
    // 延迟启动演示
    QTimer::singleShot(500, [demo]() {
        switch (demo) {
        case 1:
            demo1_CompressionComparison();
            break;
        case 2:
            demo2_CompressionPresets();
            break;
        case 3:
            demo3_LargeMessageCompression();
            break;
        default:
            qDebug() << "无效的演示编号";
            qApp->quit();
        }
    });
    
    return app.exec();
}
