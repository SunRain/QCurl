#include "QCNetworkAsyncDataPostReply.h"

#include "QCUtility.h"

namespace QCurl {


QCNetworkAsyncDataPostReply::QCNetworkAsyncDataPostReply(QObject *parent)
    : QCNetworkAsyncHttpHeadReply(parent),
      m_writePos(0)
{

}

QCNetworkAsyncDataPostReply::~QCNetworkAsyncDataPostReply()
{

}

void QCNetworkAsyncDataPostReply::setPostData(const QByteArray &data)
{
    m_postData = data;
    if (m_postData.size() > 1*1024*1024*1024) {
        set(curlHandle, CURLOPT_POSTFIELDSIZE_LARGE, m_postData.size());
    } else {
        set(curlHandle, CURLOPT_POSTFIELDSIZE, m_postData.size());
    }
    set(curlHandle, CURLOPT_POST, long(1));
}

bool QCNetworkAsyncDataPostReply::createEasyHandle(QCNetworkAccessManager *mgr, const QCNetworkRequest &req)
{
    if (!QCNetworkAsyncHttpHeadReply::createEasyHandle(mgr, req)) {
        return false;
    }
    this->setReceivedContentType(CurlEasyHandleInitializtionClass::BodyAndHeader);
    return true;
}

size_t QCNetworkAsyncDataPostReply::readFunc(char *data, size_t size, size_t nitems)
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

void QCNetworkAsyncDataPostReply::perform()
{
    QCNetworkAsyncHttpHeadReply::perform();
}


} //QCurl
