#include "QCNetworkAsyncHttpHeadReply.h"

#include <QDebug>

#include "QCUtility.h"
#include "QCNetworkAsyncHttpHeadReply_p.h"

namespace QCurl {

QCNetworkAsyncHttpHeadReply::QCNetworkAsyncHttpHeadReply(QObject *parent)
    : QCNetworkAsyncReply(parent/*, new QCNetworkHttpHeadReplyPrivate(this)*/)
//      d_ptr(static_cast<QCNetworkHttpHeadReplyPrivate*>(dPtr()))
{
//    qDebug()<<Q_FUNC_INFO<<"-------- this "<<this;
}

QCNetworkAsyncHttpHeadReply::~QCNetworkAsyncHttpHeadReply()
{
    qDebug()<<Q_FUNC_INFO<<"--------------";
//    if (d_ptr) {
//        delete d_ptr;
//    }
}

bool QCNetworkAsyncHttpHeadReply::createEasyHandle(QCNetworkAccessManager *mgr, const QCNetworkRequest &req)
{
    if (!QCNetworkAsyncReply::createEasyHandle(mgr, req)) {
        qDebug()<<Q_FUNC_INFO<<"create basic EasyHandle error";
        return false;
    }
//    if (!set(curlHandle, CURLOPT_NOBODY, 1L)) {
//        //TODO
//    }
    this->setReceivedContentType(CurlEasyHandleInitializtionClass::HeaderOnly);
    return true;
}

size_t QCNetworkAsyncHttpHeadReply::headerFunc(char *data, size_t size, size_t nitems)
{
//    qDebug()<<Q_FUNC_INFO<<"---------------";
    m_headerData.append(data, static_cast<int>(size*nitems));
    QString header = QString::fromUtf8(data, static_cast<int>(size * nitems));
    qDebug()<<Q_FUNC_INFO<<"header "<<header;
//    QStringList list = header.trimmed().split(":");
//    if (list.size() > 1) {
//        QString key = list.takeFirst();
//        QString value = list.join("");
//        qDebug()<<Q_FUNC_INFO<<"now header "<<key<<"   "<<value;
//        m_headerMap.insert(key, value);
//    }
    const int pos = header.trimmed().indexOf(":");
    if (pos > 0) {
        QString key = header.mid(0, pos).simplified();
        QString value = header.mid(pos+1, header.length()-pos-1).simplified();
//        qDebug()<<Q_FUNC_INFO<<"now header "<<key<<"   "<<value;
        m_headerMap.insert(key, value);
    }
    return size * nitems;
}

} //namespace QCurl
