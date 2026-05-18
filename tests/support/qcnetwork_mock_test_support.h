/**
 * @file
 * @brief 测试专用 MockHandler 注入辅助。
 */

#ifndef QCNETWORK_MOCK_TEST_SUPPORT_H
#define QCNETWORK_MOCK_TEST_SUPPORT_H

#include "QCNetworkTestSupport.h"

namespace QCurl::TestSupport {

inline void setMockHandler(QCNetworkAccessManager &manager, QCNetworkMockHandler *handler)
{
    setMockHandler(&manager, handler);
}

inline QCNetworkMockHandler *mockHandler(const QCNetworkAccessManager &manager)
{
    return mockHandler(&manager);
}

} // namespace QCurl::TestSupport

#endif // QCNETWORK_MOCK_TEST_SUPPORT_H
