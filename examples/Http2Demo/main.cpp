/**
 * @file main.cpp
 * @brief HTTP/2 演示程序
 * 
 * 演示 HTTP/1.1 vs HTTP/2 的性能差异：
 * - 连接建立时间对比
 * - 并发请求性能对比
 * - 多路复用演示
 * - 头部压缩效果
 * 
 */

#include <QCoreApplication>
#include <QTimer>
#include <QElapsedTimer>
#include <QCommandLineParser>
#include <iostream>
#include "QCNetworkAccessManager.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"
#include "QCNetworkHttpVersion.h"

using namespace QCurl;

class Http2Demo : public QObject
{
    Q_OBJECT

public:
    explicit Http2Demo(QObject *parent = nullptr) : QObject(parent)
    {
        manager = new QCNetworkAccessManager(this);
    }

    void demo1_SingleRequest()
    {
        std::cout << "\n=== 演示 1：单个请求对比 ===" << std::endl;
        std::cout << "测试 URL: https://http2.golang.org/reqinfo" << std::endl;
        
        // HTTP/1.1 请求
        std::cout << "\n[HTTP/1.1] 发起请求..." << std::endl;
        QElapsedTimer timer1;
        timer1.start();
        
        QCNetworkRequest request1(QUrl("https://http2.golang.org/reqinfo"));
        request1.setHttpVersion(QCNetworkHttpVersion::Http1_1);
        
        auto *reply1 = manager->sendGet(request1);
        connect(reply1, &QCNetworkReply::finished, [this, timer1, reply1]() mutable {
            qint64 elapsed1 = timer1.elapsed();
            std::cout << "[HTTP/1.1] 完成，耗时: " << elapsed1 << " ms" << std::endl;
            
            if (auto data = reply1->readAll()) {
                std::cout << "[HTTP/1.1] 响应大小: " << data->size() << " 字节" << std::endl;
            }
            
            reply1->deleteLater();
            
            // 然后测试 HTTP/2
            std::cout << "\n[HTTP/2] 发起请求..." << std::endl;
            QElapsedTimer timer2;
            timer2.start();
            
            QCNetworkRequest request2(QUrl("https://http2.golang.org/reqinfo"));
            request2.setHttpVersion(QCNetworkHttpVersion::Http2);
            
            auto *reply2 = manager->sendGet(request2);
            connect(reply2, &QCNetworkReply::finished, [this, timer2, reply2, elapsed1]() mutable {
                qint64 elapsed2 = timer2.elapsed();
                std::cout << "[HTTP/2] 完成，耗时: " << elapsed2 << " ms" << std::endl;
                
                if (auto data = reply2->readAll()) {
                    std::cout << "[HTTP/2] 响应大小: " << data->size() << " 字节" << std::endl;
                }
                
                reply2->deleteLater();
                
                // 计算性能提升
                double improvement = ((double)(elapsed1 - elapsed2) / elapsed1) * 100.0;
                std::cout << "\n✅ 性能提升: " << improvement << "%" << std::endl;
                
                emit demoComplete();
            });
        });
    }

    void demo2_ConcurrentRequests()
    {
        std::cout << "\n=== 演示 2：并发请求对比 ===" << std::endl;
        std::cout << "测试 5 个并发请求" << std::endl;
        
        // HTTP/1.1 并发
        std::cout << "\n[HTTP/1.1] 发起 5 个并发请求..." << std::endl;
        http11Timer.start();
        http11Count = 0;
        
        for (int i = 0; i < 5; ++i) {
            QCNetworkRequest req(QUrl(QString("https://http2.golang.org/reqinfo?req=%1").arg(i+1)));
            req.setHttpVersion(QCNetworkHttpVersion::Http1_1);
            
            auto *reply = manager->sendGet(req);
            connect(reply, &QCNetworkReply::finished, [this, reply, i]() {
                http11Count++;
                std::cout << "[HTTP/1.1] 请求 " << (i+1) << " 完成" << std::endl;
                reply->deleteLater();
                
                if (http11Count == 5) {
                    qint64 http11Elapsed = http11Timer.elapsed();
                    std::cout << "[HTTP/1.1] 所有请求完成，总耗时: " << http11Elapsed << " ms" << std::endl;
                    
                    // 然后测试 HTTP/2
                    testHttp2Concurrent(http11Elapsed);
                }
            });
        }
    }

    void demo3_HttpVersionDetection()
    {
        std::cout << "\n=== 演示 3：HTTP/2 支持检测 ===" << std::endl;
        
        curl_version_info_data *ver = curl_version_info(CURLVERSION_NOW);
        
        std::cout << "libcurl 版本: " << ver->version << std::endl;
        
        if (ver->features & CURL_VERSION_HTTP2) {
            std::cout << "✅ HTTP/2 支持: 已启用" << std::endl;
            std::cout << "   nghttp2 版本: " << ver->nghttp2_version << std::endl;
        } else {
            std::cout << "❌ HTTP/2 支持: 未启用" << std::endl;
            std::cout << "   提示: libcurl 需要编译时启用 nghttp2 支持" << std::endl;
        }
        
        if (ver->features & CURL_VERSION_HTTP3) {
            std::cout << "✅ HTTP/3 支持: 已启用（实验性）" << std::endl;
        } else {
            std::cout << "ℹ️ HTTP/3 支持: 未启用（可选功能）" << std::endl;
        }
        
        std::cout << "\n其他特性:" << std::endl;
        if (ver->features & CURL_VERSION_SSL) {
            std::cout << "  ✅ SSL/TLS 支持: " << ver->ssl_version << std::endl;
        }
        if (ver->features & CURL_VERSION_BROTLI) {
            std::cout << "  ✅ Brotli 压缩支持" << std::endl;
        }
        if (ver->features & CURL_VERSION_ZSTD) {
            std::cout << "  ✅ Zstd 压缩支持" << std::endl;
        }
        
        emit demoComplete();
    }

signals:
    void demoComplete();

private:
    void testHttp2Concurrent(qint64 http11Elapsed)
    {
        std::cout << "\n[HTTP/2] 发起 5 个并发请求..." << std::endl;
        http2Timer.start();
        http2Count = 0;
        
        for (int i = 0; i < 5; ++i) {
            QCNetworkRequest req(QUrl(QString("https://http2.golang.org/reqinfo?req=%1").arg(i+1)));
            req.setHttpVersion(QCNetworkHttpVersion::Http2);
            
            auto *reply = manager->sendGet(req);
            connect(reply, &QCNetworkReply::finished, [this, reply, i, http11Elapsed]() {
                http2Count++;
                std::cout << "[HTTP/2] 请求 " << (i+1) << " 完成" << std::endl;
                reply->deleteLater();
                
                if (http2Count == 5) {
                    qint64 http2Elapsed = http2Timer.elapsed();
                    std::cout << "[HTTP/2] 所有请求完成，总耗时: " << http2Elapsed << " ms" << std::endl;
                    
                    // 计算性能提升
                    double improvement = ((double)(http11Elapsed - http2Elapsed) / http11Elapsed) * 100.0;
                    std::cout << "\n✅ 性能提升: " << improvement << "%" << std::endl;
                    std::cout << "💡 HTTP/2 多路复用：5 个请求复用单个连接，减少 TLS 握手" << std::endl;
                    
                    emit demoComplete();
                }
            });
        }
    }

private:
    QCNetworkAccessManager *manager = nullptr;
    QElapsedTimer http11Timer;
    QElapsedTimer http2Timer;
    int http11Count = 0;
    int http2Count = 0;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("Http2Demo");
    app.setApplicationVersion("2.2.1");

    QCommandLineParser parser;
    parser.setApplicationDescription("QCurl HTTP/2 演示程序");
    parser.addHelpOption();
    parser.addVersionOption();
    
    parser.addPositionalArgument("demo", "演示编号 (1, 2, 3)", "[demo]");
    
    parser.process(app);
    
    Http2Demo demo;
    
    // 选择演示
    QStringList args = parser.positionalArguments();
    int demoNum = args.isEmpty() ? 0 : args.first().toInt();
    
    if (demoNum == 0) {
        std::cout << "QCurl HTTP/2 演示程序 v2.2.1" << std::endl;
        std::cout << "=============================" << std::endl;
        std::cout << "\n可用演示：" << std::endl;
        std::cout << "  1 - 单个请求对比 (HTTP/1.1 vs HTTP/2)" << std::endl;
        std::cout << "  2 - 并发请求对比 (5 个请求)" << std::endl;
        std::cout << "  3 - HTTP/2 支持检测" << std::endl;
        std::cout << "\n用法: ./Http2Demo [demo编号]" << std::endl;
        std::cout << "示例: ./Http2Demo 1" << std::endl;
        return 0;
    }
    
    QObject::connect(&demo, &Http2Demo::demoComplete, &app, &QCoreApplication::quit);
    
    switch (demoNum) {
    case 1:
        QTimer::singleShot(0, &demo, &Http2Demo::demo1_SingleRequest);
        break;
    case 2:
        QTimer::singleShot(0, &demo, &Http2Demo::demo2_ConcurrentRequests);
        break;
    case 3:
        QTimer::singleShot(0, &demo, &Http2Demo::demo3_HttpVersionDetection);
        break;
    default:
        std::cerr << "错误：无效的演示编号 " << demoNum << std::endl;
        return 1;
    }
    
    return app.exec();
}

#include "main.moc"
