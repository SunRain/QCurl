#ifndef QCNETWORKASYNCHTTPGETREPLY_H
#define QCNETWORKASYNCHTTPGETREPLY_H

#include "QCNetworkAsyncReply.h"
//#include "qbytedata_p.h"
#include "QCNetworkAsyncHttpHeadReply.h"

#include <QFile>


namespace QCurl {

class QCNetworkHttpGetReplyPrivate;
class QCNetworkAsyncHttpGetReply : public QCNetworkAsyncHttpHeadReply
{
    Q_OBJECT
public:
    QCNetworkAsyncHttpGetReply(QObject *parent = Q_NULLPTR);
    virtual ~QCNetworkAsyncHttpGetReply();

//    QCNetworkHttpGetReplyPrivate *const d_ptr;

//    Q_DECLARE_PRIVATE(QCNetworkHttpGetReply)

    // QCNetworkReply interface
protected:
    bool createEasyHandle(QCNetworkAccessManager *mgr, const QCNetworkRequest &req) override;

    size_t writeFunc(char *data, size_t size, size_t nitems) override;
};


} //namespace QCurl
#endif // QCNETWORKASYNCHTTPGETREPLY_H
