/**
 * @file tst_LargeFileDownload.cpp
 * @brief 外部网络大文件下载测试（非门禁）
 *
 * 注意：
 * - 该用例依赖外部网络与第三方镜像站可用性，不应作为门禁证据。
 * - 仅用于在具备出口网络的环境中做“真实 HTTPS + 大体量传输”回归验证。
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkError.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkTimeoutConfig.h"

#include <QSignalSpy>
#include <QString>
#include <QtTest/QtTest>

using namespace QCurl;

namespace {

constexpr qint64 kDefaultExpectedBytes = 144969772;

bool isTruthyEnvValue(const QByteArray &value)
{
    const QByteArray normalized = value.trimmed().toLower();
    if (normalized.isEmpty()) {
        return false;
    }
    return normalized != "0" && normalized != "false" && normalized != "no"
           && normalized != "off";
}

QUrl largeFileUrlFromEnv()
{
    const QByteArray url = qgetenv("QCURL_LARGE_FILE_URL");
    if (!url.isEmpty()) {
        return QUrl(QString::fromUtf8(url));
    }

    return QUrl(QStringLiteral("https://mirrors.ustc.edu.cn/archlinux/iso/2025.11.01/"
                               "archlinux-bootstrap-2025.11.01-x86_64.tar.zst"));
}

qint64 expectedBytesFromEnv()
{
    const QByteArray rawValue = qgetenv("QCURL_LARGE_FILE_EXPECTED_BYTES");
    if (rawValue.isEmpty()) {
        return qgetenv("QCURL_LARGE_FILE_URL").isEmpty() ? kDefaultExpectedBytes : -1;
    }

    bool ok             = false;
    const qint64 parsed = QString::fromUtf8(rawValue.trimmed()).toLongLong(&ok);
    return (ok && parsed > 0) ? parsed : -1;
}

QString replySummary(QCNetworkReply *reply)
{
    return QStringLiteral("error=%1, errorString=%2, httpStatus=%3")
        .arg(static_cast<int>(reply->error()))
        .arg(reply->errorString())
        .arg(reply->httpStatusCode());
}

} // namespace

class TestLargeFileDownload : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testHttpsLargeFileDownload();

private:
    bool isExternalHeavyAllowed() const;
    QCNetworkAccessManager *m_manager = nullptr;
};

void TestLargeFileDownload::initTestCase()
{
    m_manager = new QCNetworkAccessManager(this);
}

void TestLargeFileDownload::cleanupTestCase()
{
    m_manager = nullptr;
}

bool TestLargeFileDownload::isExternalHeavyAllowed() const
{
    return isTruthyEnvValue(qgetenv("QCURL_RUN_EXTERNAL_HEAVY"));
}

void TestLargeFileDownload::testHttpsLargeFileDownload()
{
    if (!isExternalHeavyAllowed()) {
        QSKIP("外部 heavy smoke 默认关闭；设置 QCURL_RUN_EXTERNAL_HEAVY=1 后才执行真实大文件下载");
    }

    const QUrl url = largeFileUrlFromEnv();
    if (!url.isValid() || url.isEmpty()) {
        const QByteArray skipReason
            = QStringLiteral("QCURL_LARGE_FILE_URL 无效: %1").arg(url.toString()).toUtf8();
        QSKIP(skipReason.constData());
    }

    const qint64 expectedBytes = expectedBytesFromEnv();

    QCNetworkRequest preflightRequest(url);
    preflightRequest.setSslConfig(QCNetworkSslConfig::insecureConfig());

    QCNetworkTimeoutConfig preflightTimeout;
    preflightTimeout.totalTimeout = std::chrono::seconds(15);
    preflightRequest.setTimeoutConfig(preflightTimeout);

    auto *preflightReply = m_manager->sendHead(preflightRequest);
    QVERIFY(preflightReply != nullptr);

    QSignalSpy preflightFinishedSpy(preflightReply, &QCNetworkReply::finished);
    if (!preflightFinishedSpy.wait(20000)) {
        preflightReply->deleteLater();
        QSKIP("外部 large file HEAD preflight 超时，跳过以避免把远端不可用误判为产品失败");
    }

    const int preflightStatus = preflightReply->httpStatusCode();
    if (preflightReply->error() != NetworkError::NoError || preflightStatus < 200
        || preflightStatus >= 300) {
        const QByteArray skipReason
            = QStringLiteral("外部 large file HEAD preflight 未通过，跳过 heavy smoke: %1")
                  .arg(replySummary(preflightReply))
                  .toUtf8();
        preflightReply->deleteLater();
        QSKIP(skipReason.constData());
    }
    preflightReply->deleteLater();

    QCNetworkRequest request(url);

    // 外部环境可能缺少 CA 链；此用例仅验证“真实 HTTPS + 大体量传输”链路，临时禁用校验以避免误报。
    request.setSslConfig(QCNetworkSslConfig::insecureConfig());

    QCNetworkTimeoutConfig timeout;
    timeout.totalTimeout = std::chrono::seconds(120);
    request.setTimeoutConfig(timeout);

    auto *reply = m_manager->sendGet(request);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    QVERIFY(finishedSpy.wait(150000));

    QVERIFY2(reply->error() == NetworkError::NoError, qPrintable(replySummary(reply)));
    const auto data = reply->readAll();
    QVERIFY2(data.has_value(), qPrintable(replySummary(reply)));

    if (expectedBytes > 0) {
        QCOMPARE(data->size(), expectedBytes);
    } else {
        QVERIFY2(!data->isEmpty(),
                 "未提供 QCURL_LARGE_FILE_EXPECTED_BYTES 时，至少应成功下载到非空响应体");
    }

    reply->deleteLater();
}

QTEST_MAIN(TestLargeFileDownload)
#include "tst_LargeFileDownload.moc"
