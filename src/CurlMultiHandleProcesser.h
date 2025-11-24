#ifndef CURLMULTIHANDLEPROCESSER_H
#define CURLMULTIHANDLEPROCESSER_H

#include <curl/curl.h>

#include <QObject>
#include <QSet>

class QTimer;
class QSocketNotifier;

namespace QCurl {

class QCNetworkReply;
struct CurlMultiSocket;
class CurlMultiHandleProcesser : public QObject
{
    Q_OBJECT
    friend class QCNetworkReply;

protected:
    static CurlMultiHandleProcesser *instance();

private:
    explicit CurlMultiHandleProcesser(QObject *parent = nullptr);
    ~CurlMultiHandleProcesser();

protected:
    void addReply(QCNetworkReply *reply);

    void removeReply(QCNetworkReply *reply);

    void performSocketAction(curl_socket_t socketfd, int eventsBitmask);

    int curlSocketFunction(CURL *easy, curl_socket_t socketfd, int what, CurlMultiSocket *socket);

    int curlTimeFunction(int timeoutMsec);

    static int staticCurlSocketFunction(CURL *easy, curl_socket_t s, int what, void *userp, void *socketp);

    static int staticCurlTimeFunction(CURLM *multi, long timeout_ms, void *userp);

////    void timeout();
//    void socketRead(int socket);

//    void socketWrite(int socket);



private:
    QString                     m_cookieFilePath;


    QSet<QCNetworkReply*>       replyList;

    CURLM                       *curlMultiHandle;

    QTimer                      *timer;

//    curl_socket_t               socketDescriptor;

//    QSocketNotifier             *readNotifier;

//    QSocketNotifier             *writeNotifier;

//    QSocketNotifier             *errorNotifier;
};

#endif // CURLMULTIHANDLEPROCESSER_H
} //namespace QCurl
