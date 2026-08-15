/**
 * @file
 * @brief Declares the private orchestration steps for starting a network reply.
 */

#ifndef QCNETWORKREPLYEXECUTION_P_H
#define QCNETWORKREPLYEXECUTION_P_H

namespace QCurl {

class QCNetworkAccessManager;
class QCNetworkReply;

namespace Internal {

class QCNetworkReplyExecution final
{
public:
    static void run(QCNetworkReply *reply);
    [[nodiscard]] static bool tryPreferNetworkCacheFallback(QCNetworkReply *reply);

private:
    [[nodiscard]] static bool completeFromCache(QCNetworkReply *reply,
                                                QCNetworkAccessManager *manager);
    [[nodiscard]] static bool dispatchMock(QCNetworkReply *reply, QCNetworkAccessManager *manager);
    [[nodiscard]] static bool prepareNetwork(QCNetworkReply *reply, QCNetworkAccessManager *manager);
};

} // namespace Internal
} // namespace QCurl

#endif // QCNETWORKREPLYEXECUTION_P_H
