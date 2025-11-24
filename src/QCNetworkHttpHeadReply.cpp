#include "QCNetworkHttpHeadReply.h"

#include <QDebug>

#include "Utility.h"
#include "QCNetworkHttpHeadReply_p.h"

namespace QCurl {

QCNetworkHttpHeadReply::QCNetworkHttpHeadReply(QObject *parent)
    : QCNetworkReply(parent/*, new QCNetworkHttpHeadReplyPrivate(this)*/)
//      d_ptr(static_cast<QCNetworkHttpHeadReplyPrivate*>(dPtr()))
{
    qDebug()<<Q_FUNC_INFO<<"-------- this "<<this;
}

QCNetworkHttpHeadReply::~QCNetworkHttpHeadReply()
{
    qDebug()<<Q_FUNC_INFO<<"--------------";
//    if (d_ptr) {
//        delete d_ptr;
//    }
}

bool QCNetworkHttpHeadReply::createEasyHandle(QCNetworkAccessManager *mgr, const QCNetworkRequest &req)
{
    if (!QCNetworkReply::createEasyHandle(mgr, req)) {
        qDebug()<<Q_FUNC_INFO<<"create basic EasyHandle error";
        return false;
    }
    return true;
}

size_t QCNetworkHttpHeadReply::headerFunc(char *data, size_t size, size_t nitems)
{
    qDebug()<<Q_FUNC_INFO<<"---------------";
    QString header = QString::fromUtf8(data, static_cast<int>(size * nitems));
    qDebug()<<Q_FUNC_INFO<<"header "<<header;
    return size * nitems;
}


} //namespace QCurl
