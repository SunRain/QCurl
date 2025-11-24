#include "QCNetworkAccessManager.h"

#include <curl/multi.h>
#include <QSet>
#include <QTimer>
#include <QDebug>
#include <QSocketNotifier>

#include "CurlGlobalConstructor.h"
#include "QCNetworkAccessManager_p.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "QCNetworkHttpHeadReply.h"
#include "QCNetworkHttpGetReply.h"
#include "Utility.h"

namespace QCurl {

QCNetworkAccessManager::QCNetworkAccessManager(QObject *parent)
    : QObject(parent),
      d_ptr(new QCNetworkAccessManagerPrivate(this))
//      curlMultiHandle(Q_NULLPTR),
//      timer(Q_NULLPTR),
//      socketDescriptor(CURL_SOCKET_BAD),
//      readNotifier(Q_NULLPTR),
//      writeNotifier(Q_NULLPTR),
//      errorNotifier(Q_NULLPTR)
{
    CurlGlobalConstructor::instance();


}

QCNetworkAccessManager::~QCNetworkAccessManager()
{
//    if (d_ptr) {
//        delete d_ptr;
//    }
}

QString QCNetworkAccessManager::cookieFilePath() const
{
    return m_cookieFilePath;
}

void QCNetworkAccessManager::setCookieFilePath(const QString &cookieFilePath)
{
    m_cookieFilePath = cookieFilePath;
}

QCNetworkReply *QCNetworkAccessManager::head(const QCNetworkRequest &request)
{
    QCNetworkReply *reply = new QCNetworkHttpHeadReply();
    if (!reply->createEasyHandle(this, request)) {
        qDebug()<<Q_FUNC_INFO<<"create easy handle error";
        return Q_NULLPTR;
    }

    return reply;
}

QCNetworkReply *QCNetworkAccessManager::get(const QCNetworkRequest &request)
{
    QCNetworkReply *reply = new QCNetworkHttpGetReply();
    if (!reply->createEasyHandle(this, request)) {
        return Q_NULLPTR;
    }

    return reply;
}


} //namespace QCurl
















































