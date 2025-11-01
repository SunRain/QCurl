/**
 * @file benchmark_http2.cpp
 * @brief HTTP/2 性能基准测试
 * 
 * 对比 HTTP/1.1 和 HTTP/2 的性能差异：
 * - 单个请求延迟
 * - 并发请求性能（多路复用）
 * - 头部压缩效果
 * 
 * @since 2.2.0
 */

#include <QtTest>
#include <QSignalSpy>
#include "QCNetworkAccessManager.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"
#include "QCNetworkHttpVersion.h"

using namespace QCurl;

// 使用支持 HTTP/2 的测试服务器
// 注意：nghttp2.org 提供稳定的 HTTP/2 测试服务
static const QString TEST_URL_HTTP2 = QStringLiteral("https://nghttp2.org/httpbin/get");
static const QString TEST_URL_HTTP1 = QStringLiteral("https://www.google.com");

class BenchmarkHttp2 : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // 基准测试
    void benchmarkHttp1Request();
    void benchmarkHttp2Request();
    void benchmarkConcurrentRequests();
    void benchmarkConcurrentRequests_data();

private:
    QCNetworkAccessManager *manager = nullptr;
    bool waitForReply(QCNetworkReply *reply, int timeout = 30000);  // 增加到30秒超时
};

void BenchmarkHttp2::initTestCase()
{
    qDebug() << "========================================";
    qDebug() << "HTTP/2 性能基准测试";
    qDebug() << "========================================";
    qDebug() << "HTTP/1.1 测试 URL:" << TEST_URL_HTTP1;
    qDebug() << "HTTP/2 测试 URL:" << TEST_URL_HTTP2;
    qDebug() << "";

    manager = new QCNetworkAccessManager(this);
}

void BenchmarkHttp2::cleanupTestCase()
{
    delete manager;
    qDebug() << "基准测试完成";
}

bool BenchmarkHttp2::waitForReply(QCNetworkReply *reply, int timeout)
{
    if (!reply) {
        return false;
    }

    QSignalSpy spy(reply, &QCNetworkReply::finished);
    return spy.wait(timeout);
}

void BenchmarkHttp2::benchmarkHttp1Request()
{
    qDebug() << "基准测试：HTTP/1.1 单个请求";

    QBENCHMARK {
        QUrl url(TEST_URL_HTTP1);
        QCNetworkRequest request(url);
        request.setHttpVersion(QCNetworkHttpVersion::Http1_1);

        auto *reply = manager->sendGet(request);

        if (waitForReply(reply, 30000)) {  // 30秒超时
            if (auto data = reply->readAll()) {
                qDebug() << "  HTTP/1.1 响应大小:" << data->size() << "字节";
            }
        } else {
            qWarning() << "  HTTP/1.1 请求超时";
        }

        reply->deleteLater();
    }
}

void BenchmarkHttp2::benchmarkHttp2Request()
{
    qDebug() << "基准测试：HTTP/2 单个请求";

    QBENCHMARK {
        QUrl url(TEST_URL_HTTP2);
        QCNetworkRequest request(url);
        request.setHttpVersion(QCNetworkHttpVersion::Http2);

        auto *reply = manager->sendGet(request);

        if (waitForReply(reply, 30000)) {  // 30秒超时
            if (auto data = reply->readAll()) {
                qDebug() << "  HTTP/2 响应大小:" << data->size() << "字节";
            }
        } else {
            qWarning() << "  HTTP/2 请求超时";
        }

        reply->deleteLater();
    }
}

void BenchmarkHttp2::benchmarkConcurrentRequests_data()
{
    QTest::addColumn<QCNetworkHttpVersion>("httpVersion");
    QTest::addColumn<int>("concurrency");

    QTest::newRow("HTTP/1.1 x5") << QCNetworkHttpVersion::Http1_1 << 5;
    QTest::newRow("HTTP/2 x5") << QCNetworkHttpVersion::Http2 << 5;
    QTest::newRow("HTTP/1.1 x10") << QCNetworkHttpVersion::Http1_1 << 10;
    QTest::newRow("HTTP/2 x10") << QCNetworkHttpVersion::Http2 << 10;
}

void BenchmarkHttp2::benchmarkConcurrentRequests()
{
    QFETCH(QCNetworkHttpVersion, httpVersion);
    QFETCH(int, concurrency);

    QString version = (httpVersion == QCNetworkHttpVersion::Http1_1) ? "HTTP/1.1" : "HTTP/2";
    qDebug() << "基准测试：" << version << "并发" << concurrency << "个请求";

    QBENCHMARK {
        QList<QCNetworkReply*> replies;

        // 发起并发请求
        for (int i = 0; i < concurrency; ++i) {
            QString urlStr = (httpVersion == QCNetworkHttpVersion::Http1_1) ? TEST_URL_HTTP1 : TEST_URL_HTTP2;
            QUrl url(urlStr);
            QCNetworkRequest req(url);
            req.setHttpVersion(httpVersion);

            auto *reply = manager->sendGet(req);
            replies.append(reply);
        }

        // 等待所有请求完成
        int completed = 0;
        for (auto *reply : replies) {
            if (waitForReply(reply, 30000)) {  // 30秒超时
                completed++;
            }
            reply->deleteLater();
        }

        qDebug() << "  完成" << completed << "/" << concurrency << "个请求";
    }

    qDebug() << "";
    qDebug() << "预期：HTTP/2 多路复用在并发场景下应更快";
}

QTEST_MAIN(BenchmarkHttp2)
#include "benchmark_http2.moc"
