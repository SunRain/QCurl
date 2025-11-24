#include "QCNetworkAsyncHttpGetReply.h"

#include <QDebug>

#include "QCNetworkAsyncHttpGetReply_p.h"

namespace QCurl {

QCNetworkAsyncHttpGetReply::QCNetworkAsyncHttpGetReply(QObject *parent)
    : QCNetworkAsyncHttpHeadReply(parent/*, new QCNetworkHttpGetReplyPrivate(this)*/)
//      d_ptr(static_cast<QCNetworkHttpGetReplyPrivate*>(dPtr()))
{
//    QCNetworkReplyPrivate *d = dPtr();
//    this->d_ptr = static_cast<QCNetworkHttpGetReplyPrivate*>(dPtr());
//    m_file = new QFile("test.bin");
//    if (!m_file->open(QIODevice::WriteOnly)) {
//        qDebug()<<Q_FUNC_INFO<<"fialed to open device";
//    }
}

QCNetworkAsyncHttpGetReply::~QCNetworkAsyncHttpGetReply()
{
//    if (d_ptr) {
//        delete d_ptr;
//    }
}

bool QCNetworkAsyncHttpGetReply::createEasyHandle(QCNetworkAccessManager *mgr, const QCNetworkRequest &req)
{
    qDebug()<<Q_FUNC_INFO<<"----------";

    if (!QCNetworkAsyncHttpHeadReply::createEasyHandle(mgr, req)) {
        qDebug()<<Q_FUNC_INFO<<"create basic EasyHandle error";
        return false;
    }
    this->setReceivedContentType(CurlEasyHandleInitializtionClass::BodyAndHeader);

    return true;
}

size_t QCNetworkAsyncHttpGetReply::writeFunc(char *data, size_t size, size_t nitems)
{
    QByteArray ba(data, size*nitems);
//    qDebug()<<Q_FUNC_INFO<<"before buffer size is "<<buffer.bufferCount();
//    qDebug()<<Q_FUNC_INFO<<" append size is"<<(size*nitems)<<" real QByteArray size "<<ba.size();
    buffer.append(ba);

//    qint64 bytesWritten = m_file->write(data, static_cast<qint64>(size*nitems));

//    qDebug()<<Q_FUNC_INFO<<" bytesWritten "<<bytesWritten;

//    qDebug()<<Q_FUNC_INFO<<"after buffer size is "<<buffer.bufferCount();

    emit readyRead();

    return size * nitems;
}




} //namespace QCurl
