#ifndef QCNETWORKHTTPGETREPLY_H
#define QCNETWORKHTTPGETREPLY_H

#include "QCNetworkReply.h"
//#include "qbytedata_p.h"
#include "QCNetworkHttpHeadReply.h"

#include <QFile>


namespace QCurl {

class QCNetworkHttpGetReplyPrivate;
class QCNetworkHttpGetReply : public QCNetworkHttpHeadReply
{
    Q_OBJECT
public:
    QCNetworkHttpGetReply(QObject *parent = Q_NULLPTR);
    virtual ~QCNetworkHttpGetReply();

//    QCNetworkHttpGetReplyPrivate *const d_ptr;

//    Q_DECLARE_PRIVATE(QCNetworkHttpGetReply)

    // QCNetworkReply interface
protected:
    bool createEasyHandle(QCNetworkAccessManager *mgr, const QCNetworkRequest &req) override;

    size_t writeFunc(char *data, size_t size, size_t nitems) override;
};


} //namespace QCurl
#endif // QCNETWORKHTTPGETREPLY_H
