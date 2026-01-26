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
#include <QtTest/QtTest>

using namespace QCurl;

class TestLargeFileDownload : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testHttpsLargeFileDownload();

private:
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

void TestLargeFileDownload::testHttpsLargeFileDownload()
{
    // 下载真实大文件：Arch Linux bootstrap (约 138 MB)
    QCNetworkRequest request(QUrl("https://mirrors.ustc.edu.cn/archlinux/iso/2025.11.01/"
                                  "archlinux-bootstrap-2025.11.01-x86_64.tar.zst"));

    // 外部环境可能缺少 CA 链；此用例仅为“传输层/大体量”回归，临时禁用校验以避免误报。
    request.setSslConfig(QCNetworkSslConfig::insecureConfig());

    QCNetworkTimeoutConfig timeout;
    timeout.totalTimeout = std::chrono::seconds(120);
    request.setTimeoutConfig(timeout);

    auto *reply = m_manager->sendGet(request);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    QVERIFY(finishedSpy.wait(150000));

    QCOMPARE(reply->error(), NetworkError::NoError);
    const auto data = reply->readAll();
    QVERIFY(data.has_value());
    QCOMPARE(data->size(), 144969772); // 期望 138 MB (144,969,772 字节)

    reply->deleteLater();
}

QTEST_MAIN(TestLargeFileDownload)
#include "tst_LargeFileDownload.moc"
