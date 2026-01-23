#ifndef QCNETWORKACCESSMANAGERPRIVATE_H
#define QCNETWORKACCESSMANAGERPRIVATE_H

#include "QCNetworkAccessManager.h"

#include <QSet>
#include <QSocketNotifier>
#include <QTimer>

#include <curl/curl.h>

namespace QCurl {

class QCNetworkAsyncReply;
class QCNetworkAccessManagerPrivate
{
public:
    QCNetworkAccessManagerPrivate(QCNetworkAccessManager *self)
        : cookieFilePath()
        , replyList()
        , curlMultiHandle(nullptr)
        , timer(nullptr)
        , socketDescriptor(CURL_SOCKET_BAD)
        , readNotifier(nullptr)
        , writeNotifier(nullptr)
        , errorNotifier(nullptr)
        , logger(nullptr)
        , middlewares()
        , mockHandler(nullptr)
        , q_ptr(self)
    {}

    ~QCNetworkAccessManagerPrivate() = default;

    QString cookieFilePath;

    QSet<QCNetworkAsyncReply *> replyList;

    CURLM *curlMultiHandle;

    QTimer *timer;

    curl_socket_t socketDescriptor;

    QSocketNotifier *readNotifier;

    QSocketNotifier *writeNotifier;

    QSocketNotifier *errorNotifier;

    // 高级功能成员
    QCNetworkLogger *logger;
    bool debugTraceEnabled = false;
    QList<QCNetworkMiddleware *> middlewares;
    QCNetworkMockHandler *mockHandler;

    Q_DECLARE_PUBLIC(QCNetworkAccessManager)

private:
    QCNetworkAccessManager *q_ptr;
};

} // namespace QCurl

#endif // QCNETWORKACCESSMANAGERPRIVATE_H
