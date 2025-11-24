#ifndef QCNETWORKREPLY_H
#define QCNETWORKREPLY_H

#include <curl/curl.h>

#include <QObject>
#include <QPair>
#include <QList>
#include <QUrl>

#include "QCNetworkRequest.h"
#include "qbytedata_p.h"

namespace QCurl {

typedef  QPair<QByteArray, QByteArray> RawHeaderPair;

class CurlMultiHandleProcesser;
class QCNetworkAccessManager;
class QCNetworkReplyPrivate;
class QCNetworkReply : public QObject
{
    Q_OBJECT
    friend class QCNetworkAccessManager;
    friend class CurlMultiHandleProcesser;
public:
    QByteArray readAll();

    QCNetworkRequest request() const;

    QUrl url() const;

    const QList<RawHeaderPair> &rawHeaderPairs() const;

    Q_SLOT void deleteLater();

protected:
    QCNetworkReply(QObject *parent = nullptr/*, QCNetworkReplyPrivate *dptr = Q_NULLPTR*/);

    virtual ~QCNetworkReply();

//    void setRequest(const QCNetworkRequest &req);

    void onCurlMsg(CURLMsg *msg);

    virtual bool createEasyHandle(QCNetworkAccessManager *mgr, const QCNetworkRequest &req);

//    inline QCNetworkReplyPrivate *dPtr() const {
//        return d_ptr;
//    }
    virtual void xferinfoFunc(curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t ultotal, curl_off_t ulnow);

    virtual size_t headerFunc(char *data, size_t size, size_t nitems);

    virtual size_t writeFunc(char *data, size_t size, size_t nitems);

    virtual size_t readFunc(char *data, size_t size, size_t nitems);

    virtual size_t seekFunc(curl_off_t offset, int origin);

signals:
    void aborted();
    void finished();
    void readyRead();
    void progress(qint64 downloadTotal, qint64 downloadNow, qint64 uploadTotal, qint64 uploadNow);

public slots:
    void abort();

    void perform();

private:
    void removeFromProcesser();

    static int staticXferinfoFunc(void *reply,
                            curl_off_t dltotal, curl_off_t dlnow,
                            curl_off_t ultotal, curl_off_t ulnow);

    static size_t staticHeaderFunc(char *data, size_t size, size_t nitems, void *ptr);

    static size_t staticWriteFunc(char *data, size_t size, size_t nitems, void *ptr);

    static size_t staticReadFunc(char *data, size_t size, size_t nitems, void *ptr);

    static size_t staticSeekFunc(void *ptr, curl_off_t offset, int origin);

//private:
protected:
//    Q_DECLARE_PRIVATE(QCNetworkReply)
//    Q_DISABLE_COPY(QCNetworkReply)
////    QScopedPointer<QCNetworkReplyPrivate> d_ptr;
//    QCNetworkReplyPrivate *const d_ptr;

    QCNetworkRequest        m_request;

//    QUrl                    m_url;

    QCByteDataBuffer        buffer;

    QList<RawHeaderPair>    m_rawHeaderPairs;

    QCNetworkAccessManager  *manager;

    CurlMultiHandleProcesser *processer;

    CURL                    *curlHandle;

    struct curl_slist       *curlHttpHeaders;
};

} //namespace QCurl
#endif // QCNETWORKREPLY_H
