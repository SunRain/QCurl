#ifndef QCURL_MIDDLEWARE_LIFETIME_TEST_SUPPORT_H
#define QCURL_MIDDLEWARE_LIFETIME_TEST_SUPPORT_H

#include "QCNetworkMiddleware.h"
#include "QCNetworkRequest.h"

#include <QList>

#include <functional>

namespace QCurl::MiddlewareLifetimeTestSupport {

class CallbackMiddleware final : public QCNetworkMiddleware
{
public:
    std::function<void(int)> callback;
    QList<int> calls;
    std::function<void(QCNetworkReply *)> created;
    std::function<void(QCNetworkReply *)> responded;

    void onRequestPreSend(QCNetworkRequest &request) override
    {
        request.setRawHeader("X-Lifetime", "retained");
        invoke(0);
    }
    void onReplyCreated(QCNetworkReply *reply) override
    {
        invoke(1);
        if (created) {
            created(reply);
        }
    }
    void onResponseReceived(QCNetworkReply *reply) override
    {
        invoke(2);
        if (responded) {
            responded(reply);
        }
    }

private:
    void invoke(int phase)
    {
        calls.append(phase);
        if (callback) {
            callback(phase);
        }
    }
};

} // namespace QCurl::MiddlewareLifetimeTestSupport

#endif
