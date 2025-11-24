#ifndef QCNETWORKACCESSMANAGERPRIVATE_H
#define QCNETWORKACCESSMANAGERPRIVATE_H

#include <curl/curl.h>

#include <QSet>
#include <QTimer>
#include <QSocketNotifier>

#include "QCNetworkAccessManager.h"

namespace QCurl {

class QCNetworkReply;
class QCNetworkAccessManagerPrivate
{
public:
    QCNetworkAccessManagerPrivate(QCNetworkAccessManager *self)
        : q_ptr(self),
          curlMultiHandle(Q_NULLPTR),
          timer(Q_NULLPTR),
          socketDescriptor(CURL_SOCKET_BAD),
          readNotifier(Q_NULLPTR),
          writeNotifier(Q_NULLPTR),
          errorNotifier(Q_NULLPTR)
    {

    }

    ~QCNetworkAccessManagerPrivate() {
        //TODO delete ptr
    }

    QString                     cookieFilePath;


    QSet<QCNetworkReply*>       replyList;

    CURLM                       *curlMultiHandle;

    QTimer                      *timer;

    curl_socket_t               socketDescriptor;

    QSocketNotifier             *readNotifier;

    QSocketNotifier             *writeNotifier;

    QSocketNotifier             *errorNotifier;

    Q_DECLARE_PUBLIC(QCNetworkAccessManager)

private:
    QCNetworkAccessManager *q_ptr;
};


} //namespace QCurl

#endif // QCNETWORKACCESSMANAGERPRIVATE_H
