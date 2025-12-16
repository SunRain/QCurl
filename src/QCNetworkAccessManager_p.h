#ifndef QCNETWORKACCESSMANAGERPRIVATE_H
#define QCNETWORKACCESSMANAGERPRIVATE_H

#include <curl/curl.h>

#include <QSet>
#include <QTimer>
#include <QSocketNotifier>

#include "QCNetworkAccessManager.h"

namespace QCurl {

class QCNetworkAsyncReply;
class QCNetworkAccessManagerPrivate
{
public:
    QCNetworkAccessManagerPrivate(QCNetworkAccessManager *self)
        : cookieFilePath(),
          replyList(),
          curlMultiHandle(nullptr),
          timer(nullptr),
          socketDescriptor(CURL_SOCKET_BAD),
          readNotifier(nullptr),
          writeNotifier(nullptr),
          errorNotifier(nullptr),
          logger(nullptr),
          middlewares(),
          mockHandler(nullptr),
          q_ptr(self)
    {

    }

    ~QCNetworkAccessManagerPrivate() = default;

    QString                     cookieFilePath;


    QSet<QCNetworkAsyncReply*>       replyList;

    CURLM                       *curlMultiHandle;

    QTimer                      *timer;

    curl_socket_t               socketDescriptor;

    QSocketNotifier             *readNotifier;

    QSocketNotifier             *writeNotifier;

    QSocketNotifier             *errorNotifier;

    // 高级功能成员
    QCNetworkLogger *logger;
    QList<QCNetworkMiddleware*> middlewares;
    QCNetworkMockHandler *mockHandler;
    
    Q_DECLARE_PUBLIC(QCNetworkAccessManager)

private:
    QCNetworkAccessManager *q_ptr;
};


} //namespace QCurl

#endif // QCNETWORKACCESSMANAGERPRIVATE_H
