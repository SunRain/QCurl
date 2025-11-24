#ifndef QCNETWORKASYNCREPLY_H
#define QCNETWORKASYNCREPLY_H

#include <curl/curl.h>

#include <QObject>
#include <QPair>
#include <QList>
#include <QUrl>
#include <QMap>

#include "QCNetworkRequest.h"
#include "CurlEasyHandleInitializtionClass.h"
#include "qbytedata_p.h"
#include "QCUtility.h"

namespace QCurl {

typedef  QPair<QByteArray, QByteArray> RawHeaderPair;

class CurlMultiHandleProcesser;
class QCNetworkAccessManager;
class QCNetworkReplyPrivate;
class QCNetworkAsyncReply : public CurlEasyHandleInitializtionClass
{
    Q_OBJECT
    friend class QCNetworkAccessManager;
    friend class CurlMultiHandleProcesser;
public:
    QByteArray readAll();

    qint64 bytesAvailable() const;
//    QCNetworkRequest request() const;

    QUrl url() const;

    QList<RawHeaderPair> rawHeaderPairs() const;

    QByteArray rawHeaderData() const;

    bool isRunning();

    NetworkError error() const;

    QString errorString() const;

    Q_SLOT void deleteLater();

protected:
    QCNetworkAsyncReply(QObject *parent = nullptr/*, QCNetworkReplyPrivate *dptr = Q_NULLPTR*/);

    virtual ~QCNetworkAsyncReply();

//    void setRequest(const QCNetworkRequest &req);

    void onCurlMsg(CURLMsg *msg);

    virtual bool createEasyHandle(QCNetworkAccessManager *mgr, const QCNetworkRequest &req) override;

//    inline QCNetworkReplyPrivate *dPtr() const {
//        return d_ptr;
//    }
    virtual void xferinfoFunc(curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t ultotal, curl_off_t ulnow) override;

//    virtual size_t headerFunc(char *data, size_t size, size_t nitems) override;

//    virtual size_t writeFunc(char *data, size_t size, size_t nitems) override;

//    virtual size_t readFunc(char *data, size_t size, size_t nitems) override;

//    virtual size_t seekFunc(curl_off_t offset, int origin) override;

signals:
    void aborted();
    void finished();
    void readyRead();
    void progress(qint64 downloadTotal, qint64 downloadNow, qint64 uploadTotal, qint64 uploadNow);

public slots:
    void abort();

    void perform() override;

private:
    void removeFromProcesser();

//    static int staticXferinfoFunc(void *reply,
//                            curl_off_t dltotal, curl_off_t dlnow,
//                            curl_off_t ultotal, curl_off_t ulnow);

//    static size_t staticHeaderFunc(char *data, size_t size, size_t nitems, void *ptr);

//    static size_t staticWriteFunc(char *data, size_t size, size_t nitems, void *ptr);

//    static size_t staticReadFunc(char *data, size_t size, size_t nitems, void *ptr);

//    static size_t staticSeekFunc(void *ptr, curl_off_t offset, int origin);

//private:
protected:
//    Q_DECLARE_PRIVATE(QCNetworkReply)
//    Q_DISABLE_COPY(QCNetworkReply)
////    QScopedPointer<QCNetworkReplyPrivate> d_ptr;
//    QCNetworkReplyPrivate *const d_ptr;

//    QCNetworkRequest        m_request;

//    QUrl                    m_url;

    bool m_isProcessing;

    NetworkError        m_error;
    QString             m_errorString;

    QCByteDataBuffer        buffer;

//    QList<RawHeaderPair>    m_rawHeaderPairs;

    QMap<QString, QString>  m_headerMap;

    QByteArray m_headerData;

//    QCNetworkAccessManager  *manager;

    CurlMultiHandleProcesser *processer;

//    CURL                    *curlHandle;

//    struct curl_slist       *curlHttpHeaders;
};

} //namespace QCurl
#endif // QCNETWORKASYNCREPLY_H
