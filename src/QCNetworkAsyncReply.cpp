#include "QCNetworkAsyncReply.h"

#include <QDebug>

#include "QCUtility.h"
#include "QCNetworkAccessManager.h"
#include "CurlMultiHandleProcesser.h"
#include "QCNetworkAsyncReply_p.h"

namespace QCurl {

QCNetworkAsyncReply::QCNetworkAsyncReply(QObject *parent)
    : CurlEasyHandleInitializtionClass(parent),
      m_isProcessing(false),
      m_error(CURL_LAST),
//      d_ptr(dptr)
//      manager(Q_NULLPTR),
      processer(/*CurlMultiHandleProcesser::instance()*/Q_NULLPTR)
//      curlHandle(Q_NULLPTR)
//      curlHttpHeaders(Q_NULLPTR)
{
     qDebug()<<Q_FUNC_INFO<<"-------- this "<<this;//<<" d "<<d_ptr;
}

QCNetworkAsyncReply::~QCNetworkAsyncReply()
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
//    if (curlHandle) {
//        curl_easy_cleanup(curlHandle);
//        curlHandle = Q_NULLPTR;
//    }
//    if (curlHttpHeaders) {
//        curl_slist_free_all(curlHttpHeaders);
//        curlHttpHeaders = Q_NULLPTR;
//    }
    //    delete d;
}

//void QCNetworkReply::setRequest(const QCNetworkRequest &req)
//{
//    m_request = req;
//}

void QCNetworkAsyncReply::onCurlMsg(CURLMsg *msg)
{
    //TODO error code
     qDebug()<<Q_FUNC_INFO<<"message "<<msg->msg
            <<" is done "<<(msg->msg == CURLMSG_DONE)
            <<" result code "<<msg->data.result;
//           <<"data is "<<QString(static_cast<char*>(msg->data.whatever));
     if (msg->msg == CURLMSG_DONE) {
         m_error = msg->data.result;
         m_errorString = QString(curl_easy_strerror(static_cast<CURLcode>(m_error)));
         processer->removeReply(this);
         emit finished();
     }
}

bool QCNetworkAsyncReply::createEasyHandle(QCNetworkAccessManager *mgr, const QCNetworkRequest &req)
{
//    Q_D(QCNetworkReply);
    return CurlEasyHandleInitializtionClass::createEasyHandle(mgr, req);
//    if (!mgr) {
//        return false;
//    }

//    if (curlHandle) {
//        qDebug()<<Q_FUNC_INFO<<"exist curl handle";
//        return true;
//    }

//    manager = mgr;

//    curlHandle = curl_easy_init();
//    Q_ASSERT(curlHandle != Q_NULLPTR);
//    qDebug()<<Q_FUNC_INFO<<" curlHandle "<<curlHandle;

//    m_request = req;

//    if (!set(curlHandle, CURLOPT_PRIVATE, this)
//            || !set(curlHandle, CURLOPT_NOPROGRESS, long(0))
//            || !set(curlHandle, CURLOPT_XFERINFODATA, this)) {
//        curl_easy_cleanup(curlHandle);
//        curlHandle = Q_NULLPTR;
//        return false;
//    }

//    qDebug()<<Q_FUNC_INFO<<" url is "<<req.url();

//    if (!set(curlHandle, CURLOPT_URL, req.url())) {
//        qDebug()<<Q_FUNC_INFO<<" set url error ";
//        curl_easy_cleanup(curlHandle);
//        curlHandle = Q_NULLPTR;
//        return false;
//    }

//    if (!mgr->cookieFilePath().isEmpty()) {
//        qDebug()<<Q_FUNC_INFO<<" set cookie "<<mgr->cookieFilePath();

//        set(curlHandle, CURLOPT_COOKIEFILE, mgr->cookieFilePath());
//        set(curlHandle, CURLOPT_COOKIEJAR, mgr->cookieFilePath());
//    }

//    if (req.followLocation()) {
//        set(curlHandle, CURLOPT_FOLLOWLOCATION, long(1));
//    }

//    // Do not return CURL_OK in case valid server responses reporting errors.
//    set(curlHandle, CURLOPT_FAILONERROR, long(1));

//    if (!set(curlHandle, CURLOPT_XFERINFOFUNCTION, staticXferinfoFunc)) {
//        //TODO:
////        curl_easy_cleanup(curlHandle);
////        curlHandle = Q_NULLPTR;
////        return false;
//    }

//    if (!set(curlHandle, CURLOPT_HEADERDATA ,this)) {
////        qDebug()<<Q_FUNC_INFO<<"set CURLOPT_HEADERDATA error ";
////        return false;
//        //TODO:
//    }

//    if (!set(curlHandle, CURLOPT_HEADERFUNCTION, staticHeaderFunc)) {
//        //TODO:
////        curl_easy_cleanup(curlHandle);
////        curlHandle = Q_NULLPTR;
////        return false;
//    }

//    if (!set(curlHandle, CURLOPT_READDATA, this)) {
//        //TODO:
//    }
//    if (!set(curlHandle, CURLOPT_READFUNCTION, staticReadFunc)) {
//        //TODO:
//    }

//    if (!set(curlHandle, CURLOPT_WRITEDATA, this)) {
//        //TODO:
//    }
//    if (!set(curlHandle, CURLOPT_WRITEFUNCTION, staticWriteFunc)) {
//        //TODO:
//    }

//    if (!set(curlHandle, CURLOPT_SEEKDATA, this)) {
//        //TODO:
//    }
//    if (!set(curlHandle, CURLOPT_SEEKFUNCTION, staticSeekFunc)) {
//        //TODO:
//    }

//    return true;
}

void QCNetworkAsyncReply::xferinfoFunc(curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
//    qDebug()<<Q_FUNC_INFO<<"------- dltotal "<<dltotal<<" dlnow "<<dlnow
//           <<" ultotal "<<ultotal<<" ulnow "<<ulnow;
    emit this->progress(dltotal, dlnow, ultotal, ulnow);
}

QByteArray QCNetworkAsyncReply::readAll()
{
//    Q_D(QCNetworkReply);
    return buffer.readAll();
}

qint64 QCNetworkAsyncReply::bytesAvailable() const
{
    return buffer.byteAmount();
}

//QCNetworkRequest QCNetworkAsyncReply::request() const
//{
////    Q_D(const QCNetworkReply);
//    return m_request;
//}

QUrl QCNetworkAsyncReply::url() const
{
//    Q_D(const QCNetworkReply);
    return m_request.url();
}

QList<RawHeaderPair> QCNetworkAsyncReply::rawHeaderPairs() const
{
//    Q_D(const QCNetworkReply);
//    return m_rawHeaderPairs;
    QList<RawHeaderPair> list;
    foreach (const QString &key, m_headerMap.keys()) {
        QString value = m_headerMap.value(key);
        list.append(qMakePair(key.toUtf8(), value.toUtf8()));
    }
    return list;
}

QByteArray QCNetworkAsyncReply::rawHeaderData() const
{
    return m_headerData;
}

bool QCNetworkAsyncReply::isRunning()
{
    return (processer != Q_NULLPTR) && m_isProcessing;
}

NetworkError QCNetworkAsyncReply::error() const
{
    return m_error;
}

QString QCNetworkAsyncReply::errorString() const
{
    return m_errorString;
}

void QCNetworkAsyncReply::deleteLater()
{
    removeFromProcesser();
    QObject::deleteLater();
}

void QCNetworkAsyncReply::abort()
{
//    Q_D(const QCNetworkReply);
//    if (!manager) {
//        return;
//    }

    if (!isRunning()) {
        return;
    }

    removeFromProcesser();

    emit aborted();
}

void QCNetworkAsyncReply::perform()
{
    if (isRunning()) {
        return;
    }

    if (!processer) {
        processer = CurlMultiHandleProcesser::instance();
        processer->addReply(this);
    }
}

void QCNetworkAsyncReply::removeFromProcesser()
{
    if (processer) {
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
}

//int QCNetworkReply::staticXferinfoFunc(void *reply, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
//{
//    qDebug()<<Q_FUNC_INFO<<"-----------------";
//    QCNetworkReply *r = static_cast<QCNetworkReply*>(reply);
//    Q_ASSERT(r != Q_NULLPTR);
//    r->xferinfoFunc(dltotal, dlnow, ultotal, ulnow);
//    return 0;
//}

//size_t QCNetworkReply::staticHeaderFunc(char *data, size_t size, size_t nitems, void *ptr)
//{
//    qDebug()<<Q_FUNC_INFO<<"-----------------";
//    QCNetworkReply *r = static_cast<QCNetworkReply*>(ptr);
//    Q_ASSERT(r != Q_NULLPTR);
//    return r->headerFunc(data, size, nitems);
//}

//size_t QCNetworkReply::staticWriteFunc(char *data, size_t size, size_t nitems, void *ptr)
//{
//    qDebug()<<Q_FUNC_INFO<<"-----------------";
//    QCNetworkReply *r = static_cast<QCNetworkReply*>(ptr);
//    Q_ASSERT(r != Q_NULLPTR);
//    return r->writeFunc(data, size, nitems);
//}

//size_t QCNetworkReply::staticReadFunc(char *data, size_t size, size_t nitems, void *ptr)
//{
//    qDebug()<<Q_FUNC_INFO<<"-----------------";
//    QCNetworkReply *r = static_cast<QCNetworkReply*>(ptr);
//    Q_ASSERT(r != Q_NULLPTR);
//    return r->readFunc(data, size, nitems);
//}

//size_t QCNetworkReply::staticSeekFunc(void *ptr, curl_off_t offset, int origin)
//{
//    qDebug()<<Q_FUNC_INFO<<"-----------------";
//    QCNetworkReply *r = static_cast<QCNetworkReply*>(ptr);
//    Q_ASSERT(r != Q_NULLPTR);
//    return r->seekFunc(offset, origin);
//}

//QCNetworkReplyPrivate *QCNetworkReply::dPtr() const
//{
//    return d_ptr;
//}


} //namespace QCurl
