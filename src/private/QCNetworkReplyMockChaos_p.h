/**
 * @file
 * @brief Declares deterministic mock replay chaos used by internal tests.
 */

#ifndef QCNETWORKREPLYMOCKCHAOS_P_H
#define QCNETWORKREPLYMOCKCHAOS_P_H

#include "QCNetworkHttpMethod.h"

class QUrl;

namespace QCurl {

class QCNetworkReply;
class QCNetworkReplyPrivate;

namespace Internal {

struct QCNetworkMockData;

[[nodiscard]] bool startMockChaosReplay(QCNetworkReply *reply,
                                        QCNetworkReplyPrivate *replyPrivate,
                                        const QCNetworkMockData &mockData,
                                        HttpMethod method,
                                        const QUrl &url);

} // namespace Internal
} // namespace QCurl

#endif // QCNETWORKREPLYMOCKCHAOS_P_H
