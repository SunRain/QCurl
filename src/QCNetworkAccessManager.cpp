#include "QCNetworkAccessManager.h"

#include <curl/multi.h>
#include <QSet>
#include <QTimer>
#include <QDebug>
#include <QSocketNotifier>

#include "CurlGlobalConstructor.h"
#include "QCNetworkAccessManager_p.h"
#include "QCNetworkAsyncReply.h"
#include "QCNetworkAsyncReply_p.h"
#include "QCNetworkAsyncHttpHeadReply.h"
#include "QCNetworkAsyncHttpGetReply.h"
#include "QCNetworkAsyncDataPostReply.h"
#include "QCNetworkSyncReply.h"
#include "QCUtility.h"

namespace QCurl {

QCNetworkAccessManager::QCNetworkAccessManager(QObject *parent)
    : QObject(parent),
      d_ptr(new QCNetworkAccessManagerPrivate(this)),
      m_cookieModeFlag(NotOpen)
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

QCNetworkAccessManager::CookieFileModeFlag QCNetworkAccessManager::cookieFileMode() const
{
    return m_cookieModeFlag;
}

void QCNetworkAccessManager::setCookieFilePath(const QString &cookieFilePath, CookieFileModeFlag flag)
{
    m_cookieFilePath = cookieFilePath;
    m_cookieModeFlag = flag;
}

QCNetworkAsyncReply *QCNetworkAccessManager::head(const QCNetworkRequest &request)
{
    QCNetworkAsyncReply *reply = new QCNetworkAsyncHttpHeadReply();
    if (!reply->createEasyHandle(this, request)) {
        qDebug()<<Q_FUNC_INFO<<"create easy handle error";
        return Q_NULLPTR;
    }

    return reply;
}

QCNetworkAsyncReply *QCNetworkAccessManager::get(const QCNetworkRequest &request)
{
    QCNetworkAsyncReply *reply = new QCNetworkAsyncHttpGetReply();
    if (!reply->createEasyHandle(this, request)) {
        return Q_NULLPTR;
    }

    return reply;
}

QCNetworkAsyncReply *QCNetworkAccessManager::post(const QCNetworkRequest &request, const QByteArray &data)
{
    QCNetworkAsyncReply *reply = new QCNetworkAsyncDataPostReply();
    if (!reply->createEasyHandle(this, request)) {
        return Q_NULLPTR;
    }

    return reply;
}

QCNetworkSyncReply *QCNetworkAccessManager::create(const QCNetworkRequest &request)
{
    QCNetworkSyncReply *reply = new QCNetworkSyncReply();
    if (!reply->createEasyHandle(this, request)) {
        return Q_NULLPTR;
    }

    return reply;
}


} //namespace QCurl
















































