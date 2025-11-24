#ifndef CURLEASYHANDLEINITIALIZTIONCLASS_H
#define CURLEASYHANDLEINITIALIZTIONCLASS_H

#include <curl/curl.h>

#include <QObject>

#include "QCNetworkRequest.h"

namespace QCurl {

class WriteHandle;
class QCNetworkAccessManager;
class CurlEasyHandleInitializtionClass : public QObject
{
    Q_OBJECT
    friend class WriteHandle;
public:
    enum ReceivedContentType {
        BodyOnly = 0x1, //not work atm
        HeaderOnly,
        BodyAndHeader
    };

protected:
    explicit CurlEasyHandleInitializtionClass(QObject *parent = nullptr);
    virtual ~CurlEasyHandleInitializtionClass();

    void setReceivedContentType(ReceivedContentType type = BodyAndHeader);

    virtual bool createEasyHandle(QCNetworkAccessManager *mgr, const QCNetworkRequest &req);

    virtual void xferinfoFunc(curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t ultotal, curl_off_t ulnow);

    virtual size_t headerFunc(char *data, size_t size, size_t nitems);

    virtual size_t writeFunc(char *data, size_t size, size_t nitems);

    virtual size_t readFunc(char *data, size_t size, size_t nitems);

    virtual int seekFunc(curl_off_t offset, int origin);

public:
    QCNetworkRequest request() const;

protected:
    QCNetworkAccessManager  *manager;
    CURL                    *curlHandle;
    QCNetworkRequest        m_request;

public slots:
    virtual void perform() = 0;

private:
    WriteHandle *m_headerWriter;
    WriteHandle *m_bodyWriter;
    static int staticXferinfoFunc(void *reply,
                            curl_off_t dltotal, curl_off_t dlnow,
                            curl_off_t ultotal, curl_off_t ulnow);

    static size_t staticHeaderFunc(char *data, size_t size, size_t nitems, void *ptr);

    static size_t staticWriteFunc(char *data, size_t size, size_t nitems, void *ptr);

    static size_t staticReadFunc(char *data, size_t size, size_t nitems, void *ptr);

    static int staticSeekFunc(void *ptr, curl_off_t offset, int origin);
};

} //namespace QCurl

#endif // CURLEASYHANDLEINITIALIZTIONCLASS_H
