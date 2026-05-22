/**
 * @file
 * @brief 测试专用 managed reply 等待辅助。
 */

#ifndef QCNETWORK_SYNC_TEST_HELPER_H
#define QCNETWORK_SYNC_TEST_HELPER_H

#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"

#include <QtTest>

namespace QCurl::TestSupport {

inline void waitForManagedTestReply(QCNetworkReply *reply, int timeoutMs = 15000)
{
    if (!reply || reply->isFinished()) {
        return;
    }

    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    if (!finishedSpy.wait(timeoutMs) && !reply->isFinished()) {
        QTest::qFail("managed test reply did not finish in time", __FILE__, __LINE__);
    }
}

inline QCNetworkReply *sendWaitedAsyncTestReply(QCNetworkAccessManager &manager,
                                                const QCNetworkRequest &request,
                                                HttpMethod method = HttpMethod::Get,
                                                const QByteArray &body = QByteArray())
{
    QCNetworkReply *reply = nullptr;
    switch (method) {
        case HttpMethod::Head:
            reply = manager.sendHead(request);
            break;
        case HttpMethod::Get:
            reply = manager.sendGet(request);
            break;
        case HttpMethod::Post:
            reply = manager.sendPost(request, body);
            break;
        case HttpMethod::Put:
            reply = manager.sendPut(request, body);
            break;
        case HttpMethod::Patch:
            reply = manager.sendPatch(request, body);
            break;
        case HttpMethod::Delete:
            reply = body.isEmpty()
                ? manager.deleteResource(request)
                : manager.sendCustomRequest(request, QByteArrayLiteral("DELETE"), body);
            break;
        case HttpMethod::Custom:
            reply = nullptr;
            break;
    }
    waitForManagedTestReply(reply);
    return reply;
}

} // namespace QCurl::TestSupport

#endif // QCNETWORK_SYNC_TEST_HELPER_H
