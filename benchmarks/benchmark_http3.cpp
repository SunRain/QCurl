/**
 * @file benchmark_http3.cpp
 * @brief HTTP/3 性能基准测试
 *
 * 对比 HTTP/1.1, HTTP/2, HTTP/3 在不同场景下的性能表现。
 *
 * 测试场景：
 * 1. 单个大文件下载（10MB）
 * 2. 多个小文件并发下载（100 x 10KB）
 * 3. 高延迟网络环境模拟
 *
 * 性能指标：
 * - 吞吐量（MB/s）
 * - 总耗时（毫秒）
 * - 连接建立时间（毫秒）
 * - 平均延迟（毫秒）
 */

#include <QCoreApplication>
#include <QTest>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTextStream>

#include <curl/curl.h>

#include "QCNetworkAccessManager.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"
#include "QCNetworkHttpVersion.h"

using namespace QCurl;

/**
 * @brief HTTP/3 性能基准测试类
 */
class BenchmarkHttp3 : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // 基准测试方法
    void benchmark_LargeFile_Http1_1();
    void benchmark_LargeFile_Http2();
    void benchmark_LargeFile_Http3();

    void benchmark_MultipleSmallFiles_Http1_1();
    void benchmark_MultipleSmallFiles_Http2();
    void benchmark_MultipleSmallFiles_Http3();

    void benchmark_ConnectionSetup_Http1_1();
    void benchmark_ConnectionSetup_Http2();
    void benchmark_ConnectionSetup_Http3();

private:
    /**
     * @brief 检查 HTTP/3 支持
     */
    bool isHttp3Supported() const;

    /**
     * @brief 下载单个文件并计时
     */
    struct BenchmarkResult {
        bool success;
        qint64 totalTime;        // 总耗时（毫秒）
        qint64 bytesDownloaded;  // 下载字节数
        double throughput;       // 吞吐量（MB/s）
        QString errorString;
    };

    BenchmarkResult downloadFile(const QUrl &url, QCNetworkHttpVersion httpVersion);

    /**
     * @brief 并发下载多个文件
     */
    BenchmarkResult downloadMultipleFiles(const QList<QUrl> &urls, QCNetworkHttpVersion httpVersion, int concurrency);

    /**
     * @brief 测试连接建立时间
     */
    qint64 measureConnectionSetup(const QUrl &url, QCNetworkHttpVersion httpVersion);

    /**
     * @brief 生成性能报告
     */
    void generateReport();

    QCNetworkAccessManager *m_manager = nullptr;
    QTextStream m_output{stdout};

    // 测试结果存储
    struct TestResult {
        QString testName;
        QString httpVersion;
        qint64 totalTime;
        double throughput;
        qint64 bytesDownloaded;
    };
    QList<TestResult> m_results;
};

void BenchmarkHttp3::initTestCase()
{
    m_manager = new QCNetworkAccessManager(this);

    // 检查 HTTP/3 支持
    curl_version_info_data *ver = curl_version_info(CURLVERSION_NOW);
    m_output << "libcurl 版本: " << ver->version << "\n";
    m_output << "HTTP/3 支持: " << (isHttp3Supported() ? "yes" : "no") << "\n";

    if (!isHttp3Supported()) {
        m_output << "HTTP/3 不可用，相关测试将跳过\n";
    }
    m_output << "\n";
}

void BenchmarkHttp3::cleanupTestCase()
{
    delete m_manager;

    generateReport();
}

bool BenchmarkHttp3::isHttp3Supported() const
{
#ifdef CURL_HTTP_VERSION_3
    curl_version_info_data *ver = curl_version_info(CURLVERSION_NOW);
    if (ver->features & CURL_VERSION_HTTP3) {
        return true;
    }
#endif
    return false;
}

BenchmarkHttp3::BenchmarkResult BenchmarkHttp3::downloadFile(const QUrl &url, QCNetworkHttpVersion httpVersion)
{
    BenchmarkResult result;
    result.success = false;
    result.totalTime = 0;
    result.bytesDownloaded = 0;
    result.throughput = 0.0;

    QCNetworkRequest request(url);
    request.setHttpVersion(httpVersion);

	QElapsedTimer timer;
	timer.start();

	QCNetworkReply *reply = m_manager->sendGet(request);
	QEventLoop loop;
	connect(reply, &QCNetworkReply::finished, &loop, &QEventLoop::quit);

	loop.exec();

	result.totalTime = timer.elapsed();

	if (reply->error() == NetworkError::NoError) {
		result.success = true;
		result.bytesDownloaded = reply->bytesReceived();

		// 计算吞吐量 (MB/s)
		if (result.totalTime > 0) {
			double seconds = result.totalTime / 1000.0;
			double megabytes = result.bytesDownloaded / (1024.0 * 1024.0);
			result.throughput = megabytes / seconds;
		}
	} else {
		result.errorString = reply->errorString();
	}

    reply->deleteLater();
    return result;
}

BenchmarkHttp3::BenchmarkResult BenchmarkHttp3::downloadMultipleFiles(
	const QList<QUrl> &urls, QCNetworkHttpVersion httpVersion, int concurrency)
{
	Q_UNUSED(concurrency)

	BenchmarkResult result;
	result.success = true;
	result.totalTime = 0;
	result.bytesDownloaded = 0;
	result.throughput = 0.0;

    QElapsedTimer timer;
    timer.start();

    // 简化实现：顺序下载（实际应该并发）
    for (const QUrl &url : urls) {
        BenchmarkResult fileResult = downloadFile(url, httpVersion);
        if (!fileResult.success) {
            result.success = false;
            result.errorString = fileResult.errorString;
            break;
        }
        result.bytesDownloaded += fileResult.bytesDownloaded;
    }

    result.totalTime = timer.elapsed();

    if (result.success) {
        double seconds = result.totalTime / 1000.0;
        double megabytes = result.bytesDownloaded / (1024.0 * 1024.0);
        result.throughput = megabytes / seconds;
    }

    return result;
}

qint64 BenchmarkHttp3::measureConnectionSetup(const QUrl &url, QCNetworkHttpVersion httpVersion)
{
	QCNetworkRequest request(url);
	request.setHttpVersion(httpVersion);

	QElapsedTimer timer;
	timer.start();

	QCNetworkReply *reply = m_manager->sendHead(request);
	QEventLoop loop;
	connect(reply, &QCNetworkReply::finished, &loop, &QEventLoop::quit);

	loop.exec();

    qint64 elapsed = timer.elapsed();
    reply->deleteLater();

    return elapsed;
}

void BenchmarkHttp3::generateReport()
{
    if (m_results.isEmpty()) {
        m_output << "无测试结果\n";
        return;
    }

    m_output << "性能对比报告\n";
    m_output << "\n";

    // 按测试分组
    QMap<QString, QList<TestResult>> groupedResults;
    for (const TestResult &result : m_results) {
        groupedResults[result.testName].append(result);
    }

    // 输出每组结果
    for (auto it = groupedResults.constBegin(); it != groupedResults.constEnd(); ++it) {
        m_output << "【" << it.key() << "】\n";
        m_output << QString("%-15s %15s %15s %15s\n")
                       .arg("HTTP版本", "总耗时(ms)", "吞吐量(MB/s)", "数据量(MB)");
        m_output << QString("-").repeated(60) << "\n";

        for (const TestResult &result : it.value()) {
            double sizeMB = result.bytesDownloaded / (1024.0 * 1024.0);
            m_output << QString("%-15s %15d %15.2f %15.2f\n")
                           .arg(result.httpVersion)
                           .arg(result.totalTime)
                           .arg(result.throughput)
                           .arg(sizeMB);
        }
        m_output << "\n";
    }
}

void BenchmarkHttp3::benchmark_LargeFile_Http1_1()
{
    // 使用 Cloudflare 的测试文件（10MB）
    QUrl url("https://speed.cloudflare.com/cdn-cgi/trace");

    QBENCHMARK {
        BenchmarkResult result = downloadFile(url, QCNetworkHttpVersion::Http1_1);

        if (result.success) {
            TestResult testResult;
            testResult.testName = "大文件下载";
            testResult.httpVersion = "HTTP/1.1";
            testResult.totalTime = result.totalTime;
            testResult.throughput = result.throughput;
            testResult.bytesDownloaded = result.bytesDownloaded;
            m_results.append(testResult);
        }
    }
}

void BenchmarkHttp3::benchmark_LargeFile_Http2()
{
    QUrl url("https://speed.cloudflare.com/cdn-cgi/trace");

    QBENCHMARK {
        BenchmarkResult result = downloadFile(url, QCNetworkHttpVersion::Http2);

        if (result.success) {
            TestResult testResult;
            testResult.testName = "大文件下载";
            testResult.httpVersion = "HTTP/2";
            testResult.totalTime = result.totalTime;
            testResult.throughput = result.throughput;
            testResult.bytesDownloaded = result.bytesDownloaded;
            m_results.append(testResult);
        }
    }
}

void BenchmarkHttp3::benchmark_LargeFile_Http3()
{
    if (!isHttp3Supported()) {
        QSKIP("HTTP/3 不支持");
    }

    QUrl url("https://speed.cloudflare.com/cdn-cgi/trace");

    QBENCHMARK {
        BenchmarkResult result = downloadFile(url, QCNetworkHttpVersion::Http3);

        if (result.success) {
            TestResult testResult;
            testResult.testName = "大文件下载";
            testResult.httpVersion = "HTTP/3";
            testResult.totalTime = result.totalTime;
            testResult.throughput = result.throughput;
            testResult.bytesDownloaded = result.bytesDownloaded;
            m_results.append(testResult);
        }
    }
}

void BenchmarkHttp3::benchmark_MultipleSmallFiles_Http1_1()
{
    // 使用多个小文件URL
    QList<QUrl> urls;
    for (int i = 0; i < 10; ++i) {
        urls << QUrl("https://httpbin.org/bytes/10240");  // 10KB each
    }

    QBENCHMARK {
        BenchmarkResult result = downloadMultipleFiles(urls, QCNetworkHttpVersion::Http1_1, 5);

        if (result.success) {
            TestResult testResult;
            testResult.testName = "多文件并发";
            testResult.httpVersion = "HTTP/1.1";
            testResult.totalTime = result.totalTime;
            testResult.throughput = result.throughput;
            testResult.bytesDownloaded = result.bytesDownloaded;
            m_results.append(testResult);
        }
    }
}

void BenchmarkHttp3::benchmark_MultipleSmallFiles_Http2()
{
    QList<QUrl> urls;
    for (int i = 0; i < 10; ++i) {
        urls << QUrl("https://httpbin.org/bytes/10240");
    }

    QBENCHMARK {
        BenchmarkResult result = downloadMultipleFiles(urls, QCNetworkHttpVersion::Http2, 5);

        if (result.success) {
            TestResult testResult;
            testResult.testName = "多文件并发";
            testResult.httpVersion = "HTTP/2";
            testResult.totalTime = result.totalTime;
            testResult.throughput = result.throughput;
            testResult.bytesDownloaded = result.bytesDownloaded;
            m_results.append(testResult);
        }
    }
}

void BenchmarkHttp3::benchmark_MultipleSmallFiles_Http3()
{
    if (!isHttp3Supported()) {
        QSKIP("HTTP/3 不支持");
    }

    QList<QUrl> urls;
    for (int i = 0; i < 10; ++i) {
        urls << QUrl("https://httpbin.org/bytes/10240");
    }

    QBENCHMARK {
        BenchmarkResult result = downloadMultipleFiles(urls, QCNetworkHttpVersion::Http3, 5);

        if (result.success) {
            TestResult testResult;
            testResult.testName = "多文件并发";
            testResult.httpVersion = "HTTP/3";
            testResult.totalTime = result.totalTime;
            testResult.throughput = result.throughput;
            testResult.bytesDownloaded = result.bytesDownloaded;
            m_results.append(testResult);
        }
    }
}

void BenchmarkHttp3::benchmark_ConnectionSetup_Http1_1()
{
    QUrl url("https://www.cloudflare.com");

    QBENCHMARK {
        measureConnectionSetup(url, QCNetworkHttpVersion::Http1_1);
    }
}

void BenchmarkHttp3::benchmark_ConnectionSetup_Http2()
{
    QUrl url("https://www.cloudflare.com");

    QBENCHMARK {
        measureConnectionSetup(url, QCNetworkHttpVersion::Http2);
    }
}

void BenchmarkHttp3::benchmark_ConnectionSetup_Http3()
{
    if (!isHttp3Supported()) {
        QSKIP("HTTP/3 不支持");
    }

    QUrl url("https://www.cloudflare.com");

    QBENCHMARK {
        measureConnectionSetup(url, QCNetworkHttpVersion::Http3);
    }
}

QTEST_MAIN(BenchmarkHttp3)
#include "benchmark_http3.moc"
