#include "QCNetworkReply.h"

#include <QDebug>

#include "Utility.h"
#include "QCNetworkAccessManager.h"
#include "CurlMultiHandleProcesser.h"
#include "QCNetworkReply_p.h"

namespace QCurl {

QCNetworkReply::QCNetworkReply(QObject *parent)
    : QObject(parent),
//      d_ptr(dptr)
      manager(Q_NULLPTR),
      processer(CurlMultiHandleProcesser::instance()),
      curlHandle(Q_NULLPTR),
      curlHttpHeaders(Q_NULLPTR)
{
     qDebug()<<Q_FUNC_INFO<<"-------- this "<<this;//<<" d "<<d_ptr;
}

QCNetworkReply::~QCNetworkReply()
{
    qDebug()<<Q_FUNC_INFO<<"-----------";

    removeFromProcesser();

//    if (curlHandle) {
//        curl_easy_cleanup(curlHandle);
//        curlHandle = Q_NULLPTR;
//    }
//    if (curlHttpHeaders) {
//        curl_slist_free_all(curlHttpHeaders);
//        curlHttpHeaders = Q_NULLPTR;
//    }
//    Q_D(const QCNetworkReply)
    if (curlHandle) {
        curl_easy_cleanup(curlHandle);
        curlHandle = Q_NULLPTR;
    }
    if (curlHttpHeaders) {
        curl_slist_free_all(curlHttpHeaders);
        curlHttpHeaders = Q_NULLPTR;
    }
    //    delete d;
}

//void QCNetworkReply::setRequest(const QCNetworkRequest &req)
//{
//    m_request = req;
//}

void QCNetworkReply::onCurlMsg(CURLMsg *msg)
{
     qDebug()<<Q_FUNC_INFO<<"message "<<msg->msg<<" is done "<<(msg->msg == CURLMSG_DONE);
     if (msg->msg == CURLMSG_DONE) {
         processer->removeReply(this);
         emit finished();
     }
}

bool QCNetworkReply::createEasyHandle(QCNetworkAccessManager *mgr, const QCNetworkRequest &req)
{
//    Q_D(QCNetworkReply);
    if (!mgr) {
        return false;
    }

    if (curlHandle) {
        qDebug()<<Q_FUNC_INFO<<"exist curl handle";
        return true;
    }

    manager = mgr;

    curlHandle = curl_easy_init();
    Q_ASSERT(curlHandle != Q_NULLPTR);
    qDebug()<<Q_FUNC_INFO<<" curlHandle "<<curlHandle;

    m_request = req;

    if (!set(curlHandle, CURLOPT_PRIVATE, this)
            || !set(curlHandle, CURLOPT_NOPROGRESS, long(0))
            || !set(curlHandle, CURLOPT_XFERINFODATA, this)) {
        curl_easy_cleanup(curlHandle);
        curlHandle = Q_NULLPTR;
        return false;
    }

    qDebug()<<Q_FUNC_INFO<<" url is "<<req.url();

    if (!set(curlHandle, CURLOPT_URL, req.url())) {
        qDebug()<<Q_FUNC_INFO<<" set url error ";
        curl_easy_cleanup(curlHandle);
        curlHandle = Q_NULLPTR;
        return false;
    }

    if (req.followLocation()) {
        set(curlHandle, CURLOPT_FOLLOWLOCATION, long(1));
    }

    // Do not return CURL_OK in case valid server responses reporting errors.
    set(curlHandle, CURLOPT_FAILONERROR, long(1));

    if (!set(curlHandle, CURLOPT_XFERINFOFUNCTION, staticXferinfoFunc)) {
        //TODO:
//        curl_easy_cleanup(curlHandle);
//        curlHandle = Q_NULLPTR;
//        return false;
    }

    if (!set(curlHandle, CURLOPT_HEADERDATA ,this)) {
//        qDebug()<<Q_FUNC_INFO<<"set CURLOPT_HEADERDATA error ";
//        return false;
        //TODO:
    }

    if (!set(curlHandle, CURLOPT_HEADERFUNCTION, staticHeaderFunc)) {
        //TODO:
//        curl_easy_cleanup(curlHandle);
//        curlHandle = Q_NULLPTR;
//        return false;
    }

    if (!set(curlHandle, CURLOPT_READDATA, this)) {
        //TODO:
    }
    if (!set(curlHandle, CURLOPT_READFUNCTION, staticReadFunc)) {
        //TODO:
    }

    if (!set(curlHandle, CURLOPT_WRITEDATA, this)) {
        //TODO:
    }
    if (!set(curlHandle, CURLOPT_WRITEFUNCTION, staticWriteFunc)) {
        //TODO:
    }

    if (!set(curlHandle, CURLOPT_SEEKDATA, this)) {
        //TODO:
    }
    if (!set(curlHandle, CURLOPT_SEEKFUNCTION, staticSeekFunc)) {
        //TODO:
    }

    return true;
}

void QCNetworkReply::xferinfoFunc(curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    qDebug()<<Q_FUNC_INFO<<"------- dltotal "<<dltotal<<" dlnow "<<dlnow
           <<" ultotal "<<ultotal<<" ulnow "<<ulnow;
    emit this->progress(dltotal, dlnow, ultotal, ulnow);
}

size_t QCNetworkReply::headerFunc(char *data, size_t size, size_t nitems)
{
    return 0;
}

size_t QCNetworkReply::writeFunc(char *data, size_t size, size_t nitems)
{
    return 0;
}

size_t QCNetworkReply::readFunc(char *data, size_t size, size_t nitems)
{
    return 0;
}

size_t QCNetworkReply::seekFunc(curl_off_t offset, int origin)
{
    return  0;
}

QByteArray QCNetworkReply::readAll()
{
//    Q_D(QCNetworkReply);
    return buffer.readAll();
}

QCNetworkRequest QCNetworkReply::request() const
{
//    Q_D(const QCNetworkReply);
    return m_request;
}

QUrl QCNetworkReply::url() const
{
//    Q_D(const QCNetworkReply);
    return m_request.url();
}

const QList<RawHeaderPair> &QCNetworkReply::rawHeaderPairs() const
{
//    Q_D(const QCNetworkReply);
    return m_rawHeaderPairs;
}

void QCNetworkReply::deleteLater()
{
    removeFromProcesser();
    QObject::deleteLater();
}

void QCNetworkReply::abort()
{
//    Q_D(const QCNetworkReply);
//    if (!manager) {
//        return;
//    }
    removeFromProcesser();

    emit aborted();
}

void QCNetworkReply::perform()
{
    processer->addReply(this);
}

void QCNetworkReply::removeFromProcesser()
{
    qDebug()<<Q_FUNC_INFO<<"------------";
//    Q_D(QCNetworkReply);
//    if (!manager) {
//        qWarning()<<Q_FUNC_INFO<<"Nullptr QCNetworkAccessManager";
//        return;
//    }
//    manager->removeReply(this);
    processer->removeReply(this);
//    manager = Q_NULLPTR;
}

int QCNetworkReply::staticXferinfoFunc(void *reply, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    qDebug()<<Q_FUNC_INFO<<"-----------------";
    QCNetworkReply *r = static_cast<QCNetworkReply*>(reply);
    Q_ASSERT(r != Q_NULLPTR);
    r->xferinfoFunc(dltotal, dlnow, ultotal, ulnow);
    return 0;
}

size_t QCNetworkReply::staticHeaderFunc(char *data, size_t size, size_t nitems, void *ptr)
{
    qDebug()<<Q_FUNC_INFO<<"-----------------";
    QCNetworkReply *r = static_cast<QCNetworkReply*>(ptr);
    Q_ASSERT(r != Q_NULLPTR);
    return r->headerFunc(data, size, nitems);
}

size_t QCNetworkReply::staticWriteFunc(char *data, size_t size, size_t nitems, void *ptr)
{
    qDebug()<<Q_FUNC_INFO<<"-----------------";
    QCNetworkReply *r = static_cast<QCNetworkReply*>(ptr);
    Q_ASSERT(r != Q_NULLPTR);
    return r->writeFunc(data, size, nitems);
}

size_t QCNetworkReply::staticReadFunc(char *data, size_t size, size_t nitems, void *ptr)
{
    qDebug()<<Q_FUNC_INFO<<"-----------------";
    QCNetworkReply *r = static_cast<QCNetworkReply*>(ptr);
    Q_ASSERT(r != Q_NULLPTR);
    return r->readFunc(data, size, nitems);
}

size_t QCNetworkReply::staticSeekFunc(void *ptr, curl_off_t offset, int origin)
{
    qDebug()<<Q_FUNC_INFO<<"-----------------";
    QCNetworkReply *r = static_cast<QCNetworkReply*>(ptr);
    Q_ASSERT(r != Q_NULLPTR);
    return r->seekFunc(offset, origin);
}

//QCNetworkReplyPrivate *QCNetworkReply::dPtr() const
//{
//    return d_ptr;
//}


} //namespace QCurl
