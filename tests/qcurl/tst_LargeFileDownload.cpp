/**
 * @file tst_LargeFileDownload.cpp
 * @brief 外部网络大文件下载测试（非门禁）
 *
 * 注意：
 * - 该用例依赖外部网络与第三方镜像站可用性，不应作为门禁证据。
 * - 仅用于在具备出口网络的环境中做“真实 HTTPS + 大体量传输”回归验证。
 */

#include "QCGlobal.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkError.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkTimeoutConfig.h"

#include <QElapsedTimer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QString>
#include <QtTest/QtTest>

#include <algorithm>
#include <chrono>
#include <memory>

using namespace QCurl;

namespace {

bool isTruthyEnvValue(const QByteArray &value)
{
    const QByteArray normalized = value.trimmed().toLower();
    if (normalized.isEmpty()) {
        return false;
    }
    return normalized != "0" && normalized != "false" && normalized != "no" && normalized != "off";
}

QUrl largeFileUrlFromEnv()
{
    return QUrl(QString::fromUtf8(qgetenv("QCURL_LARGE_FILE_URL").trimmed()), QUrl::StrictMode);
}

qint64 expectedBytesFromEnv()
{
    const QByteArray rawValue = qgetenv("QCURL_LARGE_FILE_EXPECTED_BYTES").trimmed();
    if (rawValue.isEmpty() || !std::all_of(rawValue.cbegin(), rawValue.cend(), [](char ch) {
            return ch >= '0' && ch <= '9';
        })) {
        return -1;
    }
    bool ok             = false;
    const qint64 parsed = rawValue.toLongLong(&ok);
    return (ok && parsed > 0) ? parsed : -1;
}

QCNetworkRequest largeFileRequest(const QUrl &url, std::chrono::seconds totalTimeout)
{
    QCNetworkRequest request(url);
    request.setRawHeader(QByteArrayLiteral("User-Agent"),
                         QByteArrayLiteral("QCurl/") + versionString());
    // 仅验证 HTTPS 大体量传输，不以外部环境的 CA 链作为本 smoke 的通过前提。
    request.setSslConfig(QCNetworkSslConfig::insecureConfig());
    QCNetworkTimeoutConfig timeout;
    timeout.setTotalTimeout(totalTimeout);
    request.setTimeoutConfig(timeout);
    return request;
}

QString replySummary(const QCNetworkReply *reply, qint64 elapsedMs, qint64 expectedBytes)
{
    return QStringLiteral("error=%1, errorString=%2, httpStatus=%3, curlCode=%4, "
                          "receivedBytes=%5/%6, elapsedMs=%7")
        .arg(static_cast<int>(reply->error()))
        .arg(reply->errorString())
        .arg(reply->httpStatusCode())
        .arg(reply->diagnosticCurlCode())
        .arg(reply->bytesAvailable())
        .arg(expectedBytes)
        .arg(elapsedMs);
}

} // namespace

class TestLargeFileDownload : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    void testHttpsLargeFileDownload();

private:
    void verifyPreflight(const QUrl &url);
    void verifyDownload(const QUrl &url, qint64 expectedBytes);
    std::unique_ptr<QCNetworkAccessManager> m_manager;
};

void TestLargeFileDownload::initTestCase()
{
    m_manager = std::make_unique<QCNetworkAccessManager>();
}

void TestLargeFileDownload::cleanupTestCase()
{
    m_manager.reset();
}

void TestLargeFileDownload::testHttpsLargeFileDownload()
{
    if (!isTruthyEnvValue(qgetenv("QCURL_RUN_EXTERNAL_HEAVY"))) {
        QSKIP("外部 heavy smoke 默认关闭；设置 QCURL_RUN_EXTERNAL_HEAVY=1 后才执行真实大文件下载");
    }

    const QUrl url = largeFileUrlFromEnv();
    QVERIFY2(url.isValid() && !url.isEmpty() && url.scheme() == QStringLiteral("https")
                 && !url.host().isEmpty(),
             "启用外部 heavy smoke 时必须通过 QCURL_LARGE_FILE_URL 提供有效 HTTPS URL");
    const qint64 expectedBytes = expectedBytesFromEnv();
    QVERIFY2(expectedBytes > 0,
             "QCURL_LARGE_FILE_EXPECTED_BYTES 必须是该文件的十进制正整数字节数，且不能超出 qint64");

    verifyPreflight(url);
    if (QTest::currentTestResolved()) {
        return;
    }
    verifyDownload(url, expectedBytes);
}

void TestLargeFileDownload::verifyPreflight(const QUrl &url)
{
    QElapsedTimer timer;
    timer.start();
    auto *reply = m_manager->head(largeFileRequest(url, std::chrono::seconds(15)));
    QVERIFY(reply != nullptr);
    const auto cleanup = qScopeGuard([reply]() { reply->deleteLater(); });
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    if (!reply->isFinished() && !finishedSpy.wait(20000)) {
        const auto reason = QStringLiteral(
                                "外部 large file HEAD preflight 超时，跳过 heavy smoke: %1")
                                .arg(replySummary(reply, timer.elapsed(), 0));
        reply->cancel();
        QSKIP(qPrintable(reason));
    }
    const int status = reply->httpStatusCode();
    if (reply->error() != NetworkError::NoError || status < 200 || status >= 300) {
        const auto reason = QStringLiteral(
                                "外部 large file HEAD preflight 未通过，跳过 heavy smoke: %1")
                                .arg(replySummary(reply, timer.elapsed(), 0));
        QSKIP(qPrintable(reason));
    }
}

void TestLargeFileDownload::verifyDownload(const QUrl &url, qint64 expectedBytes)
{
    QElapsedTimer timer;
    timer.start();
    auto *reply = m_manager->get(largeFileRequest(url, std::chrono::seconds(120)));
    QVERIFY(reply != nullptr);
    const auto cleanup = qScopeGuard([reply]() { reply->deleteLater(); });
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    const bool finished = reply->isFinished() || finishedSpy.wait(150000);
    const auto summary  = replySummary(reply, timer.elapsed(), expectedBytes);
    qInfo().noquote() << "外部大文件下载:" << summary;
    if (!finished) {
        reply->cancel();
    }
    QVERIFY2(finished, qPrintable(summary));
    QVERIFY2(reply->error() == NetworkError::NoError, qPrintable(summary));
    const auto data = reply->readAll();
    QVERIFY2(data.has_value(), qPrintable(summary));
    QCOMPARE(data->size(), expectedBytes);
}

QTEST_MAIN(TestLargeFileDownload)
#include "tst_LargeFileDownload.moc"
