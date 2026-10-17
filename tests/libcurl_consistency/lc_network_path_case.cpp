#include "lc_network_path_case.h"

#include "QCNetworkError.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "qcnetwork_managed_reply_wait_helper.h"

#include <QFile>
#include <QStringList>
#include <QUrl>
#include <QtTest>

#include <curl/curl.h>

using namespace QCurl;

namespace {

bool configureNetworkPathRequest(QCNetworkRequest &request)
{
    const QString isolation = qEnvironmentVariable("QCURL_LC_RESOLVE_ISOLATION");
    if (isolation != QStringLiteral("isolated") && isolation != QStringLiteral("native_shared")) {
        return false;
    }
    const auto strategy         = isolation == QStringLiteral("isolated")
                                      ? QCNetworkResolveIsolation::Isolated
                                      : QCNetworkResolveIsolation::NativeShared;
    const QStringList resolve   = qEnvironmentVariable("QCURL_LC_RESOLVE_ENTRIES")
                                      .split(QChar('\n'), Qt::SkipEmptyParts);
    const QStringList connectTo = qEnvironmentVariable("QCURL_LC_CONNECT_TO_ENTRIES")
                                      .split(QChar('\n'), Qt::SkipEmptyParts);
    const bool connectFirst     = qEnvironmentVariableIntValue("QCURL_LC_CONNECT_FIRST") != 0;
    request.setHttpVersion(QCNetworkHttpVersion::Http1_1);
    if (connectFirst) {
        request.setConnectTo(connectTo);
    }
    if (request.setResolveOverride(resolve) != QCNetworkConfigUpdateResult::Applied
        || request.setResolveIsolation(strategy) != QCNetworkConfigUpdateResult::Applied) {
        return false;
    }
    if (!connectFirst) {
        request.setConnectTo(connectTo);
    }
    return request.resolveIsolation() == strategy
           && request.resolveOverride().value_or(QStringList()) == resolve
           && request.connectTo().value_or(QStringList()) == connectTo;
}

} // namespace

void runNetworkPathCase(QCNetworkAccessManager &manager)
{
    const QUrl url(qEnvironmentVariable("QCURL_LC_TARGET_URL"));
    QVERIFY(url.isValid() && !url.host().isEmpty());
    QCNetworkRequest request(url);
    QVERIFY(configureNetworkPathRequest(request));

    auto *reply = TestSupport::sendWaitedAsyncTestReply(manager, request);
    QVERIFY(reply);
    QVERIFY(reply->isFinished());
    const int expectedCurlCode = qEnvironmentVariableIntValue("QCURL_LC_EXPECT_CURL_CODE");
    QCOMPARE(reply->diagnosticCurlCode(), expectedCurlCode);
    QCOMPARE(reply->error(), fromCurlCode(static_cast<CURLcode>(expectedCurlCode)));
    QCOMPARE(reply->httpStatusCode(), expectedCurlCode == 0 ? 200 : 0);
    qInfo().noquote() << QStringLiteral("curlcode=%1 http_code=%2")
                             .arg(reply->diagnosticCurlCode())
                             .arg(reply->httpStatusCode());

    const auto body = reply->readAll();
    QVERIFY(body.has_value());
    if (expectedCurlCode == 0) {
        QVERIFY(!body->isEmpty());
    } else {
        QVERIFY(body->isEmpty());
    }
    // 保留原场景对已完成 reply 的消费语义检查，重复读取不能重放正文。
    const auto secondBody = reply->readAll();
    QVERIFY(secondBody.has_value() && secondBody->isEmpty());
    const auto tail = reply->readAll();
    QVERIFY(tail.has_value() && tail->isEmpty());

    QFile output(QStringLiteral("download_0.data"));
    QVERIFY(output.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(output.write(*body), body->size());
    QVERIFY(output.flush());
    reply->deleteLater();
}
