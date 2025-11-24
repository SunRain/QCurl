#include "QCNetworkHttpGetReply.h"

#include <QDebug>

#include "QCNetworkHttpGetReply_p.h"

namespace QCurl {

QCNetworkHttpGetReply::QCNetworkHttpGetReply(QObject *parent)
    : QCNetworkHttpHeadReply(parent/*, new QCNetworkHttpGetReplyPrivate(this)*/)
//      d_ptr(static_cast<QCNetworkHttpGetReplyPrivate*>(dPtr()))
{
//    QCNetworkReplyPrivate *d = dPtr();
//    this->d_ptr = static_cast<QCNetworkHttpGetReplyPrivate*>(dPtr());
//    m_file = new QFile("test.bin");
//    if (!m_file->open(QIODevice::WriteOnly)) {
//        qDebug()<<Q_FUNC_INFO<<"fialed to open device";
//    }
}

QCNetworkHttpGetReply::~QCNetworkHttpGetReply()
{
//    if (d_ptr) {
//        delete d_ptr;
//    }
}

bool QCNetworkHttpGetReply::createEasyHandle(QCNetworkAccessManager *mgr, const QCNetworkRequest &req)
{
    qDebug()<<Q_FUNC_INFO<<"----------";

    if (!QCNetworkHttpHeadReply::createEasyHandle(mgr, req)) {
        qDebug()<<Q_FUNC_INFO<<"create basic EasyHandle error";
        return false;
    }


    return true;
}

size_t QCNetworkHttpGetReply::writeFunc(char *data, size_t size, size_t nitems)
{
    QByteArray ba(data, size*nitems);
    qDebug()<<Q_FUNC_INFO<<"before buffer size is "<<buffer.bufferCount();
    qDebug()<<Q_FUNC_INFO<<" append size is"<<(size*nitems)<<" real QByteArray size "<<ba.size();
//    m_buffer.append(ba);
    buffer.append(ba);

//    qint64 bytesWritten = m_file->write(data, static_cast<qint64>(size*nitems));

//    qDebug()<<Q_FUNC_INFO<<" bytesWritten "<<bytesWritten;

    qDebug()<<Q_FUNC_INFO<<"after buffer size is "<<buffer.bufferCount();

    emit readyRead();

    return size * nitems;
}




} //namespace QCurl
