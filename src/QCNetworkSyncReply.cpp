#include "QCNetworkSyncReply.h"

#include <QDebug>

#include "QCUtility.h"

namespace QCurl {

QCNetworkSyncReply::QCNetworkSyncReply(QObject *parent)
    : CurlEasyHandleInitializtionClass(parent),
//      m_headerWriter(new HeaderWriter(this)),
      m_writePos(0),
      m_error(CURL_LAST)
{
//    CurlEasyHandleInitializtionClass::setReceivedContentType(BodyAndHeader);
}

QCNetworkSyncReply::~QCNetworkSyncReply()
{
//    if (m_headerWriter) {
//        m_headerWriter->deleteLater();
//        m_headerWriter = Q_NULLPTR;
    //    }
}

void QCNetworkSyncReply::setReceivedContentType(CurlEasyHandleInitializtionClass::ReceivedContentType type)
{
    CurlEasyHandleInitializtionClass::setReceivedContentType(type);
}

NetworkError QCNetworkSyncReply::error() const
{
    return m_error;
}

QString QCNetworkSyncReply::errorString() const
{
    return m_errorString;
}

QList<RawHeaderPair> QCNetworkSyncReply::rawHeaderPairs() const
{
    QList<RawHeaderPair> list;
    foreach (const QString &key, m_headerMap.keys()) {
        QString value = m_headerMap.value(key);
        list.append(qMakePair(key.toUtf8(), value.toUtf8()));
    }
    return list;
}

QByteArray QCNetworkSyncReply::rawHeaderData() const
{
    return m_rawHeaderData;
}

void QCNetworkSyncReply::setPostData(const QByteArray &data)
{
    m_postData = data;
    if (m_postData.size() > 1*1024*1024*1024) {
        set(curlHandle, CURLOPT_POSTFIELDSIZE_LARGE, m_postData.size());
    } else {
        set(curlHandle, CURLOPT_POSTFIELDSIZE, m_postData.size());
    }
    set(curlHandle, CURLOPT_POST, long(1));
}

//void QCNetworkSyncReply::setReadFunction(const QCNetworkSyncReply::DataFunction &func)
//{
//    m_readFunction = func;
//}

void QCNetworkSyncReply::setWriteFunction(const QCNetworkSyncReply::DataFunction &func)
{
    m_writeFunction = func;
}

void QCNetworkSyncReply::setCustomHeaderFunction(const QCNetworkSyncReply::DataFunction &func)
{
    m_headerFunction = func;
}

void QCNetworkSyncReply::setSeekFunction(const QCNetworkSyncReply::SeekFunction &func)
{
    m_seekFunction = func;
}

void QCNetworkSyncReply::setProgressFunction(const QCNetworkSyncReply::ProgressFunction &func)
{
    m_progressFunction = func;
}

void QCNetworkSyncReply::deleteLater()
{
    if (curlHandle) {
        curl_easy_cleanup(curlHandle);
        curlHandle = Q_NULLPTR;
    }
    QObject::deleteLater();
}


//bool QCNetworkSyncReply::isRunning()
//{
//    return true;
//}


void QCNetworkSyncReply::perform()
{
    if (!curlHandle) {
        qWarning()<<Q_FUNC_INFO<<"No curl handle!!";
        return;
    }
    char errbuf[CURL_ERROR_SIZE];
    set(curlHandle, CURLOPT_ERRORBUFFER, errbuf);
    errbuf[0] = 0;
    m_error = curl_easy_perform(curlHandle);
    if (m_error != CURLE_OK) {
        qDebug()<<Q_FUNC_INFO<<"perform error "<<m_error;
        size_t len = strlen(errbuf);
        if (len) {
            m_errorString = QString(errbuf);
        } else {
            m_errorString = QString(curl_easy_strerror(static_cast<CURLcode>(m_error)));
        }
    }
}

void QCNetworkSyncReply::abort()
{
    if (curlHandle) {
        curl_easy_cleanup(curlHandle);
        curlHandle = Q_NULLPTR;
    }
}

//size_t QCNetworkSyncReply::staticHeaderFunc(char *data, size_t size, size_t nitems, void *ptr)
//{
//    HeaderWriter *r = static_cast<HeaderWriter*>(ptr);
//    Q_ASSERT(r != Q_NULLPTR);
//    return r->headerFunc(data, size, nitems);
//}

bool QCNetworkSyncReply::createEasyHandle(QCNetworkAccessManager *mgr, const QCNetworkRequest &req)
{
    if (!CurlEasyHandleInitializtionClass::createEasyHandle(mgr, req)) {
        return false;
    }
//    qDebug()<<Q_FUNC_INFO<<" this addr "<<this;
//    if (!set(curlHandle, CURLOPT_HEADERDATA, m_headerWriter)) {
//        //TODO:
//    }

//    if (!set(curlHandle, CURLOPT_HEADERFUNCTION, staticHeaderFunc)) {
//        //TODO:
//    }
    this->setReceivedContentType(BodyAndHeader);
    return true;
}

void QCNetworkSyncReply::xferinfoFunc(curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    if (m_progressFunction) {
        m_progressFunction(static_cast<qint64>(dltotal), static_cast<qint64>(dlnow),
                           static_cast<qint64>(ultotal), static_cast<qint64>(ulnow));
    }
}

size_t QCNetworkSyncReply::headerFunc(char *data, size_t size, size_t nitems)
{
    if (m_headerFunction) {
        return m_headerFunction(data, size * nitems);
    }

    QByteArray hd(data, static_cast<int>(size*nitems));
    m_rawHeaderData.append(hd);
    QString header(hd);
    qDebug()<<Q_FUNC_INFO<<"header "<<header;
    const int pos = header.trimmed().indexOf(":");
    if (pos > 0) {
        QString key = header.mid(0, pos).simplified();
        QString value = header.mid(pos+1, header.length()-pos-1).simplified();
        qDebug()<<Q_FUNC_INFO<<"now header "<<key<<"   "<<value;
        m_headerMap.insert(key, value);
    }
    return size * nitems;
}

size_t QCNetworkSyncReply::writeFunc(char *data, size_t size, size_t nitems)
{
    if (m_writeFunction) {
        return m_writeFunction(data, size * nitems);
    }
    return size * nitems;
}

size_t QCNetworkSyncReply::readFunc(char *data, size_t size, size_t nitems)
{
    int available = (m_postData.size() - m_writePos);
    if (available > 0) {
        int written = qMin(available, static_cast<int>(size * nitems));
        memcpy(data, m_postData.constData() + m_writePos, written);
        m_writePos += written;
        return written;
    }
    return 0;
}

int QCNetworkSyncReply::seekFunc(curl_off_t offset, int origin)
{
    if (m_seekFunction) {
        return m_seekFunction(static_cast<qint64>(offset), origin);
    }
    return 0;
}

} //namespace QCurl
