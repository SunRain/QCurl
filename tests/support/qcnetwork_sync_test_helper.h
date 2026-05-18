/**
 * @file
 * @brief 测试专用同步 reply 构造辅助。
 */

#ifndef QCNETWORK_SYNC_TEST_HELPER_H
#define QCNETWORK_SYNC_TEST_HELPER_H

#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"

namespace QCurl::TestSupport {

inline QCNetworkReply *sendSyncTestReply(QCNetworkAccessManager &manager,
                                         const QCNetworkRequest &request,
                                         HttpMethod method = HttpMethod::Get,
                                         const QByteArray &body = QByteArray())
{
    auto *reply = new QCNetworkReply(QCNetworkReply::TestOnlyKey{},
                                     request,
                                     method,
                                     ExecutionMode::Sync,
                                     body,
                                     &manager);
    reply->execute();
    return reply;
}

} // namespace QCurl::TestSupport

#endif // QCNETWORK_SYNC_TEST_HELPER_H
