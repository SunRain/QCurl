#include "CurlMultiHandleProcesser.h"

#include <QSet>
#include <QTimer>
#include <QDebug>
#include <QSocketNotifier>
#include <QThreadStorage>

//#include "CurlGlobalConstructor.h"
//#include "QCNetworkAccessManager_p.h"
#include "QCNetworkReply.h"
//#include "QCNetworkReply_p.h"
//#include "QCNetworkHttpHeadReply.h"
//#include "QCNetworkHttpGetReply.h"
#include "Utility.h"

namespace QCurl {

struct CurlMultiSocket
{
    curl_socket_t socketDescriptor = CURL_SOCKET_BAD;
    QSocketNotifier *readNotifier = nullptr;
    QSocketNotifier *writeNotifier = nullptr;
    QSocketNotifier *errorNotifier = nullptr;
};

static void deleter(CurlMultiHandleProcesser *obj)
{
    qDebug()<<Q_FUNC_INFO<<"------------ QSharedPointer<CurlMultiHandleProcesser> deleter";
    obj->deleteLater();
}
CurlMultiHandleProcesser *CurlMultiHandleProcesser::instance()
{
    static QThreadStorage<QSharedPointer<CurlMultiHandleProcesser>> instance;
    if (!instance.hasLocalData()) {
        instance.setLocalData(QSharedPointer<CurlMultiHandleProcesser>(new CurlMultiHandleProcesser,
                                                                       deleter));
        qDebug()<<Q_FUNC_INFO<<"----------------------no localdata ";
    }
    qDebug()<<Q_FUNC_INFO<<"exist "<<instance.localData().data();
    return instance.localData().data();
}

CurlMultiHandleProcesser::CurlMultiHandleProcesser(QObject *parent)
    : QObject(parent),
      curlMultiHandle(Q_NULLPTR),
      timer(Q_NULLPTR)
    //      socketDescriptor(CURL_SOCKET_BAD),
    //      readNotifier(Q_NULLPTR),
    //      writeNotifier(Q_NULLPTR),
    //      errorNotifier(Q_NULLPTR)
{
    curlMultiHandle = curl_multi_init();
    Q_ASSERT(curlMultiHandle != Q_NULLPTR);

    CURLMcode ret = curl_multi_setopt(curlMultiHandle, CURLMOPT_SOCKETDATA, this);
    Q_ASSERT(ret == CURLM_OK);

    ret = curl_multi_setopt(curlMultiHandle, CURLMOPT_SOCKETFUNCTION, staticCurlSocketFunction);
    Q_ASSERT(ret == CURLM_OK);

    ret = curl_multi_setopt(curlMultiHandle, CURLMOPT_TIMERDATA, this);
    Q_ASSERT(ret == CURLM_OK);

    ret = curl_multi_setopt(curlMultiHandle, CURLMOPT_TIMERFUNCTION, staticCurlTimeFunction);
    Q_ASSERT(ret == CURLM_OK);

    timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout,
            [&](){
        qDebug()<<Q_FUNC_INFO<<"----------- "<<CURL_SOCKET_TIMEOUT;
        performSocketAction(CURL_SOCKET_TIMEOUT, 0);
    });
    //    connect(timer, &QTimer::timeout, this, &QCNetworkAccessManager::timeout);

}

CurlMultiHandleProcesser::~CurlMultiHandleProcesser()
{
    //TODO delete

}

void CurlMultiHandleProcesser::addReply(QCNetworkReply *reply)
{
    replyList.insert(reply);
    qDebug()<<Q_FUNC_INFO<<"curlMultiHandle "<<curlMultiHandle<<" reply->curlHandle "<<reply->curlHandle
           <<" reply "<<reply;
    CURLMcode ret = curl_multi_add_handle(curlMultiHandle, reply->curlHandle);
    qDebug()<<Q_FUNC_INFO<<" ret "<<ret;
}

void CurlMultiHandleProcesser::removeReply(QCNetworkReply *reply)
{
    if (replyList.contains(reply)) {
        curl_multi_remove_handle(curlMultiHandle, reply->curlHandle);
        replyList.remove(reply);
    }
}

void CurlMultiHandleProcesser::performSocketAction(curl_socket_t socketfd, int eventsBitmask)
{
    //    Q_D(QCNetworkAccessManager);d->
    qDebug()<<"curl_socket_t "<<socketfd<<" eventsBitmask "<<eventsBitmask;
    //    qDebug()<<Q_FUNC_INFO<<" d "<<d;
    qDebug()<<Q_FUNC_INFO<<" multi-handle "<<curlMultiHandle;
    int runningHandles = 0;
    CURLMcode rc = curl_multi_socket_action(curlMultiHandle,
                                            socketfd,
                                            eventsBitmask,
                                            &runningHandles);
    if (rc != CURLM_OK) {
        // TODO: Handle global curl errors
    }
    qDebug()<<Q_FUNC_INFO<<"--------------";
    int messagesLeft = 0;
    do {
        CURLMsg *message = curl_multi_info_read(curlMultiHandle, &messagesLeft);

        if (!message)
            break;

        if (!message->easy_handle)
            continue;

        QCNetworkReply *reply = Q_NULLPTR;
        curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, &reply);

        if (!reply)
            continue;

        long response;
        CURLcode ret = curl_easy_getinfo(message->easy_handle, CURLINFO_RESPONSE_CODE, &response);
        if (ret == CURLE_OK) {
            qDebug()<<Q_FUNC_INFO<<" response "<<response;
        }
        char *location = Q_NULLPTR;
        ret = curl_easy_getinfo(message->easy_handle, CURLINFO_REDIRECT_URL, &location);
        if ((ret == CURLE_OK) && location) {
            qDebug()<<Q_FUNC_INFO<<" CURLINFO_REDIRECT_URL "<<location;
        }
        //TODO emit done
        reply->onCurlMsg(message);
//        qDebug()<<Q_FUNC_INFO<<"message "<<message->msg<<" is done "<<(message->msg == CURLMSG_DONE);

    } while (messagesLeft);
}

int CurlMultiHandleProcesser::curlSocketFunction(CURL *easy, curl_socket_t socketfd, int what, CurlMultiSocket *socket)
{
    qDebug()<<Q_FUNC_INFO<<"--------------------";
    qDebug()<<"curl_socket_t "<<socketfd<<" action "<<what;

    //    if (what == CURL_POLL_REMOVE) {
    //        if (readNotifier) {
    //            delete readNotifier;
    //            readNotifier = Q_NULLPTR;
    //        }
    //        if (writeNotifier) {
    //            delete writeNotifier;
    //            writeNotifier = Q_NULLPTR;
    //        }
    //        if (errorNotifier) {
    //            delete errorNotifier;
    //            errorNotifier = Q_NULLPTR;
    //        }
    //        socketDescriptor = CURL_SOCKET_BAD;
    //        return 0;
    //    }
    //    if (socketDescriptor == CURL_SOCKET_BAD) {
    //        socketDescriptor = socketfd;
    //    }
    //    if (what == CURL_POLL_NONE) {
    //        return 0;
    //    }
    if (!socket) {
        if (what == CURL_POLL_REMOVE || what == CURL_POLL_NONE)
            return 0;

        socket = new CurlMultiSocket;
        socket->socketDescriptor = socketfd;
        curl_multi_assign(curlMultiHandle, socketfd, socket);
    }

    if (what == CURL_POLL_REMOVE) {
        curl_multi_assign(curlMultiHandle, socketfd, nullptr);

        // Note: deleteLater will NOT work here since there are
        //       situations where curl subscribes same sockect descriptor
        //       until events processing is done and actual delete happen.
        //       This causes QSocketNotifier not to register notifications again.
        if (socket->readNotifier) delete socket->readNotifier;
        if (socket->writeNotifier) delete socket->writeNotifier;
        if (socket->errorNotifier) delete socket->errorNotifier;
        delete socket;
        return 0;
    }
    if (what == CURL_POLL_IN || what == CURL_POLL_INOUT) {
        if (!socket->readNotifier) {
            socket->readNotifier = new QSocketNotifier(socket->socketDescriptor,
                                                       QSocketNotifier::Read);
            connect(socket->readNotifier,
                    &QSocketNotifier::activated,
                    [&](int socket) {
                qDebug()<<Q_FUNC_INFO<<"readNotifier, socket fd "<<socket;
                performSocketAction(socket, CURL_CSELECT_IN);
            });
            //            connect(readNotifier,
            //                    &QSocketNotifier::activated,
            //                    this,
            //                    &QCNetworkAccessManager::socketRead);
            //        }
            socket->readNotifier->setEnabled(true);
        } else {
            if (socket->readNotifier) {
                socket->readNotifier->setEnabled(false);
            }
        }
    }
    if (what == CURL_POLL_OUT || what == CURL_POLL_INOUT) {
        if (!socket->writeNotifier) {
            socket->writeNotifier = new QSocketNotifier(socket->socketDescriptor,
                                                        QSocketNotifier::Write);
            connect(socket->writeNotifier,
                    &QSocketNotifier::activated,
                    [&](int socket) {
                qDebug()<<Q_FUNC_INFO<<"writeNotifier, socket fd "<<socket;
                performSocketAction(socket, CURL_CSELECT_OUT);
            });
            //            connect(writeNotifier,
            //                    &QSocketNotifier::activated,
            //                    this,
            //                    &QCNetworkAccessManager::socketWrite);
        }
        socket->writeNotifier->setEnabled(true);
    } else {
        if (socket->writeNotifier) {
            socket->writeNotifier->setEnabled(false);
        }
    }
    return 0;
}

int CurlMultiHandleProcesser::curlTimeFunction(int timeoutMsec)
{
    //    Q_D(QCNetworkAccessManager);
    qDebug()<<Q_FUNC_INFO<<"------- "<<timeoutMsec<<" -------------";
    if (timeoutMsec >= 0) {
        qDebug()<<Q_FUNC_INFO<<"aaaaaaaaaa";
        timer->start(timeoutMsec);
    } else {
        qDebug()<<Q_FUNC_INFO<<"bbbbbbb";
        timer->stop();
    }
    return 0;
}

int CurlMultiHandleProcesser::staticCurlSocketFunction(CURL *easy, curl_socket_t s, int what, void *userp, void *socketp)
{
    qDebug()<<Q_FUNC_INFO;

    Q_UNUSED(easy);
    CurlMultiHandleProcesser *multi = static_cast<CurlMultiHandleProcesser*>(userp);
    Q_ASSERT(multi != nullptr);

    return multi->curlSocketFunction(easy, s, what, static_cast<CurlMultiSocket*>(socketp));
}

int CurlMultiHandleProcesser::staticCurlTimeFunction(CURLM *multi, long timeout_ms, void *userp)
{
    qDebug()<<Q_FUNC_INFO<<"--------------- userp(QCNetworkAccessManager) "<<userp;
    qDebug()<<Q_FUNC_INFO<<" timeout ms "<<timeout_ms;
    Q_UNUSED(multi);
    CurlMultiHandleProcesser *processer = static_cast<CurlMultiHandleProcesser*>(userp);

        qDebug()<<Q_FUNC_INFO<<" processer "<<processer;

    Q_ASSERT(processer != Q_NULLPTR);

    int intTimeoutMs;

    if (timeout_ms >= std::numeric_limits<int>::max()) {
        intTimeoutMs = std::numeric_limits<int>::max();
    } else if (timeout_ms >= 0) {
        intTimeoutMs = static_cast<int>(timeout_ms);
    }  else {
        intTimeoutMs = -1;
    }
    return processer->curlTimeFunction(intTimeoutMs);
}

//void CurlMultiHandleProcesser::socketRead(int socket)
//{
//    performSocketAction(socket, CURL_CSELECT_IN);
//}

//void CurlMultiHandleProcesser::socketWrite(int socket)
//{
//    performSocketAction(socket, CURL_CSELECT_OUT);
//}












}
