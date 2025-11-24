#ifndef QCNETWORKASYNCHEADREPLY_H
#define QCNETWORKASYNCHEADREPLY_H

#include "QCNetworkAsyncReply.h"

namespace QCurl {

class QCNetworkHttpHeadReplyPrivate;
class QCNetworkAsyncHttpHeadReply : public QCNetworkAsyncReply
{
    Q_OBJECT
public:
    explicit QCNetworkAsyncHttpHeadReply(QObject *parent = Q_NULLPTR);
    virtual ~QCNetworkAsyncHttpHeadReply();


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
#endif // QCNETWORKASYNCHEADREPLY_H
