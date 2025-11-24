#ifndef QCNETWORKASYNCDATAPOSTREPLY_H
#define QCNETWORKASYNCDATAPOSTREPLY_H

#include <QObject>

#include "QCNetworkAsyncHttpHeadReply.h"


namespace QCurl {

class QCNetworkAsyncDataPostReply : public QCNetworkAsyncHttpHeadReply
{
    friend class QCNetworkAccessManager;
public:
    explicit QCNetworkAsyncDataPostReply(QObject *parent = Q_NULLPTR);
    virtual ~QCNetworkAsyncDataPostReply();

protected:
    void setPostData(const QByteArray &data);

    // CurlEasyHandleInitializtionClass interface
protected:
    bool createEasyHandle(QCNetworkAccessManager *mgr, const QCNetworkRequest &req) override;
    size_t readFunc(char *data, size_t size, size_t nitems) override;

public slots:
    void perform() override;

private:
    QByteArray m_postData;
    int m_writePos;
};


} //namespace QCurl

#endif // QCNETWORKASYNCDATAPOSTREPLY_H
