/**
 * @file main.cpp
 * @brief HTTP/3 功能演示程序
 * 
 * 演示如何使用 QCurl 的 HTTP/3 支持，包括：
 * - 发送 HTTP/3 请求
 * - HTTP/1.1 vs HTTP/2 vs HTTP/3 性能对比
 * - 协议协商和降级处理
 * 
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"

#include <curl/curl.h>

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QTimer>

using namespace QCurl;

/**
 * @brief 检查 libcurl 是否支持 HTTP/3
 */
bool checkHttp3Support()
{
    curl_version_info_data *ver = curl_version_info(CURLVERSION_NOW);
    qDebug() << "========================================";
    qDebug() << "libcurl 版本:" << ver->version;
    qDebug() << "协议支持:" << ver->protocols;

#ifdef CURL_VERSION_HTTP3
    if (ver->features & CURL_VERSION_HTTP3) {
        qDebug() << "HTTP/3 支持: ✅ 是";
        return true;
    }
#endif

    qDebug() << "HTTP/3 支持: ❌ 否";
    qDebug() << "提示: 需要 libcurl >= 7.66.0 并编译时支持 nghttp3/ngtcp2";
    return false;
}

/**
 * @brief 演示基本的 HTTP/3 请求
 */
void demonstrateBasicHttp3Request()
{
    qDebug() << "\n========================================";
    qDebug() << "演示 1: 基本 HTTP/3 请求";
    qDebug() << "========================================";

    QCNetworkAccessManager manager;

    // 使用支持 HTTP/3 的测试服务器
    QCNetworkRequest request(QUrl("https://cloudflare-quic.com"));
    request.setHttpVersion(QCNetworkHttpVersion::Http3);

    qDebug() << "发送 HTTP/3 请求到: https://cloudflare-quic.com";

    auto *reply = manager.sendGet(request);

    QObject::connect(reply, &QCNetworkReply::finished, [reply]() {
        if (reply->error() == NetworkError::NoError) {
            auto data = reply->readAll();
            qDebug() << "✅ 请求成功!";
            // HTTP 状态码（QCNetworkReply 可能没有直接的方法，跳过）
            qDebug() << "   响应成功";
            qDebug() << "   响应大小:" << (data.has_value() ? data->size() : 0) << "字节";

            // 显示部分响应内容
            if (data.has_value() && data->size() > 0) {
                QString preview = QString::fromUtf8(data->left(200));
                qDebug() << "   响应预览:" << preview << "...";
            }
        } else {
            qDebug() << "❌ 请求失败:" << reply->errorString();
            qDebug() << "   提示: 服务器可能不支持 HTTP/3 或网络问题";
        }

        reply->deleteLater();
        QCoreApplication::quit();
    });

    reply->execute();
}

/**
 * @brief 演示 HTTP/3 降级处理
 */
void demonstrateHttp3Fallback()
{
    qDebug() << "\n========================================";
    qDebug() << "演示 2: HTTP/3 降级处理";
    qDebug() << "========================================";

    QCNetworkAccessManager manager;

    // 使用只支持 HTTP/1.1 的服务器
    QCNetworkRequest request(QUrl("https://httpbin.org/get"));
    request.setHttpVersion(QCNetworkHttpVersion::Http3);

    qDebug() << "尝试对不支持 HTTP/3 的服务器使用 HTTP/3";
    qDebug() << "目标: https://httpbin.org/get";

    auto *reply = manager.sendGet(request);

    QObject::connect(reply, &QCNetworkReply::finished, [reply]() {
        if (reply->error() == NetworkError::NoError) {
            qDebug() << "✅ 请求成功（已自动降级）";
            // HTTP 状态码（QCNetworkReply 可能没有直接的方法，跳过）
            qDebug() << "   响应成功";
            qDebug() << "   提示: libcurl 自动降级到服务器支持的协议";
        } else {
            qDebug() << "❌ 请求失败:" << reply->errorString();
        }

        reply->deleteLater();
        QCoreApplication::quit();
    });

    reply->execute();
}

/**
 * @brief 演示 Http3Only 模式
 */
void demonstrateHttp3OnlyMode()
{
    qDebug() << "\n========================================";
    qDebug() << "演示 3: Http3Only 模式（仅 HTTP/3）";
    qDebug() << "========================================";

    QCNetworkAccessManager manager;

    QCNetworkRequest request(QUrl("https://cloudflare-quic.com"));
    request.setHttpVersion(QCNetworkHttpVersion::Http3Only);

    qDebug() << "使用 Http3Only 模式（不允许降级）";
    qDebug() << "目标: https://cloudflare-quic.com";

    auto *reply = manager.sendGet(request);

    QObject::connect(reply, &QCNetworkReply::finished, [reply]() {
        if (reply->error() == NetworkError::NoError) {
            qDebug() << "✅ HTTP/3 连接成功!";
            // HTTP 状态码（QCNetworkReply 可能没有直接的方法，跳过）
            qDebug() << "   响应成功";
        } else {
            qDebug() << "❌ HTTP/3 连接失败:" << reply->errorString();
            qDebug() << "   提示: Http3Only 模式不允许降级";
        }

        reply->deleteLater();
        QCoreApplication::quit();
    });

    reply->execute();
}

/**
 * @brief 性能对比测试
 */
void performanceComparison()
{
    qDebug() << "\n========================================";
    qDebug() << "演示 4: HTTP 版本性能对比";
    qDebug() << "========================================";

    const QUrl testUrl("https://www.google.com");
    const int iterations = 3;

    struct TestResult
    {
        QString version;
        QCNetworkHttpVersion httpVersion;
        qint64 totalTime = 0;
        bool completed   = false;
    };

    QList<TestResult> results = {{"HTTP/1.1", QCNetworkHttpVersion::Http1_1, 0, false},
                                 {"HTTP/2", QCNetworkHttpVersion::Http2, 0, false},
                                 {"HTTP/3", QCNetworkHttpVersion::Http3, 0, false}};

    qDebug() << "测试 URL:" << testUrl.toString();
    qDebug() << "测试次数:" << iterations << "次";
    qDebug() << "";

    auto *manager        = new QCNetworkAccessManager();
    int currentTest      = 0;
    int currentIteration = 0;

    std::function<void()> runNextTest = [&]() {
        if (currentTest >= results.size()) {
            // 所有测试完成，显示结果
            qDebug() << "\n========================================";
            qDebug() << "性能对比结果:";
            qDebug() << "========================================";

            for (const auto &result : results) {
                if (result.completed) {
                    qint64 avgTime = result.totalTime / iterations;
                    qDebug() << result.version << "平均响应时间:" << avgTime << "ms";
                }
            }

            // 找到最快的版本
            qint64 minTime = LLONG_MAX;
            QString fastest;
            for (const auto &result : results) {
                if (result.completed && result.totalTime < minTime) {
                    minTime = result.totalTime;
                    fastest = result.version;
                }
            }

            if (!fastest.isEmpty()) {
                qDebug() << "\n🏆 最快版本:" << fastest;
            }

            delete manager;
            QCoreApplication::quit();
            return;
        }

        if (currentIteration >= iterations) {
            // 当前测试完成，进入下一个
            results[currentTest].completed = true;
            currentTest++;
            currentIteration = 0;
            runNextTest();
            return;
        }

        // 执行当前测试
        QCNetworkRequest request(testUrl);
        request.setHttpVersion(results[currentTest].httpVersion);

        QElapsedTimer timer;
        timer.start();

        auto *reply = manager->sendGet(request);

        QObject::connect(reply, &QCNetworkReply::finished, [&, reply, timer]() {
            qint64 elapsed = timer.elapsed();

            if (reply->error() == NetworkError::NoError) {
                results[currentTest].totalTime += elapsed;
                qDebug() << "  " << results[currentTest].version << "迭代" << (currentIteration + 1)
                         << ":" << elapsed << "ms";
            } else {
                qDebug() << "  " << results[currentTest].version << "迭代" << (currentIteration + 1)
                         << "失败:" << reply->errorString();
            }

            reply->deleteLater();
            currentIteration++;

            // 继续下一次迭代
            QTimer::singleShot(100, runNextTest);
        });

        reply->execute();
    };

    qDebug() << "开始性能测试...";
    runNextTest();
}

/**
 * @brief 演示 HTTP 版本协商
 */
void demonstrateVersionNegotiation()
{
    qDebug() << "\n========================================";
    qDebug() << "演示 5: HTTP 版本自动协商";
    qDebug() << "========================================";

    QCNetworkAccessManager manager;

    QCNetworkRequest request(QUrl("https://www.google.com"));
    request.setHttpVersion(QCNetworkHttpVersion::HttpAny);

    qDebug() << "使用 HttpAny 模式（让 libcurl 自动选择最优版本）";
    qDebug() << "目标: https://www.google.com";

    auto *reply = manager.sendGet(request);

    QObject::connect(reply, &QCNetworkReply::finished, [reply]() {
        if (reply->error() == NetworkError::NoError) {
            qDebug() << "✅ 请求成功!";
            // HTTP 状态码（QCNetworkReply 可能没有直接的方法，跳过）
            qDebug() << "   响应成功";
            qDebug() << "   提示: libcurl 已选择最优的 HTTP 版本";
        } else {
            qDebug() << "❌ 请求失败:" << reply->errorString();
        }

        reply->deleteLater();
        QCoreApplication::quit();
    });

    reply->execute();
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qDebug() << "========================================";
    qDebug() << "QCurl HTTP/3 功能演示";
    qDebug() << "v2.17.0";
    qDebug() << "========================================";

    // 检查 HTTP/3 支持
    bool http3Supported = checkHttp3Support();

    if (!http3Supported) {
        qDebug() << "\n警告: 当前 libcurl 不支持 HTTP/3";
        qDebug() << "某些演示将无法运行或会自动降级";
    }

    qDebug() << "\n可用演示:";
    qDebug() << "  1. 基本 HTTP/3 请求";
    qDebug() << "  2. HTTP/3 降级处理";
    qDebug() << "  3. Http3Only 模式";
    qDebug() << "  4. HTTP 版本性能对比";
    qDebug() << "  5. HTTP 版本自动协商";
    qDebug() << "";

    // 根据命令行参数选择演示
    QString demo = "1"; // 默认演示 1
    if (argc > 1) {
        demo = argv[1];
    }

    qDebug() << "运行演示:" << demo;
    qDebug() << "（可通过命令行参数选择：./Http3Demo 1-5）";

    if (demo == "1") {
        demonstrateBasicHttp3Request();
    } else if (demo == "2") {
        demonstrateHttp3Fallback();
    } else if (demo == "3") {
        demonstrateHttp3OnlyMode();
    } else if (demo == "4") {
        performanceComparison();
    } else if (demo == "5") {
        demonstrateVersionNegotiation();
    } else {
        qDebug() << "无效的演示编号，使用默认演示 1";
        demonstrateBasicHttp3Request();
    }

    return app.exec();
}
