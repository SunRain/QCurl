#ifndef QCNETWORKHEADREPLY_H
#define QCNETWORKHEADREPLY_H

#include "QCNetworkReply.h"

namespace QCurl {

class QCNetworkHttpHeadReplyPrivate;
class QCNetworkHttpHeadReply : public QCNetworkReply
{
    Q_OBJECT
public:
    QCNetworkHttpHeadReply(QObject *parent = Q_NULLPTR);
    virtual ~QCNetworkHttpHeadReply();


//    QScopedPointer<QCNetworkHttpHeadReplyPrivate> d_ptr;
//    QCNetworkHttpHeadReplyPrivate *const d_ptr;
//    Q_DECLARE_PRIVATE(QCNetworkHttpHeadReply)

    // QCNetworkReply interface
protected:
    bool createEasyHandle(QCNetworkAccessManager *mgr, const QCNetworkRequest &req) override;

    // QCNetworkReply interface
protected:
    size_t headerFunc(char *data, size_t size, size_t nitems) override;

};


} //namespace QCurl
#endif // QCNETWORKHEADREPLY_H
