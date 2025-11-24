#ifndef QCNETWORKSYNCREPLY_H
#define QCNETWORKSYNCREPLY_H

#include <QObject>
#include <QMap>

#include "CurlEasyHandleInitializtionClass.h"
#include "QCUtility.h"

namespace QCurl {

typedef  QPair<QByteArray, QByteArray> RawHeaderPair;

//class HeaderWriter;
class QCNetworkSyncReply : public CurlEasyHandleInitializtionClass
{
    Q_OBJECT
    friend class QCNetworkAccessManager;
public:
    using DataFunction          = std::function<size_t(char *buffer, size_t size)>;
    using SeekFunction          = std::function<int(qint64 offset, int origin)>;
    using ProgressFunction      = std::function<void(qint64 dltotal, qint64 dlnow,
                                                 qint64 ultotal, qint64 ulnow)>;

    explicit QCNetworkSyncReply(QObject *parent = nullptr);
    virtual ~QCNetworkSyncReply();

    void setReceivedContentType(ReceivedContentType type = BodyAndHeader);

    NetworkError error() const;

    QString errorString() const;

    QList<RawHeaderPair> rawHeaderPairs() const;

    QByteArray rawHeaderData() const;

    void setPostData(const QByteArray &data);

//    void setReadFunction(const DataFunction &func);
    void setWriteFunction(const DataFunction &func);
    void setCustomHeaderFunction(const DataFunction &func);
    void setSeekFunction(const SeekFunction &func);
    void setProgressFunction(const ProgressFunction &func);

    Q_SLOT void deleteLater();

    // CurlEasyHandleInitializtionClass interface
protected:
    bool createEasyHandle(QCNetworkAccessManager *mgr, const QCNetworkRequest &req) override;
    void xferinfoFunc(curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) override;
    size_t headerFunc(char *data, size_t size, size_t nitems) override;
    size_t writeFunc(char *data, size_t size, size_t nitems) override;
    size_t readFunc(char *data, size_t size, size_t nitems) override;
    int seekFunc(curl_off_t offset, int origin) override;

public slots:
    void perform() override;

    void abort();

private:
//    static size_t staticHeaderFunc(char *data, size_t size, size_t nitems, void *ptr);

private:
//    HeaderWriter *m_headerWriter;
//    DataFunction        m_readFunction;
    DataFunction        m_writeFunction;
    DataFunction        m_headerFunction;
    SeekFunction        m_seekFunction;
    ProgressFunction    m_progressFunction;

    int                 m_writePos;
    NetworkError        m_error;
    QString             m_errorString;
    QByteArray          m_postData;
    QByteArray          m_rawHeaderData;
    QMap<QString, QString>  m_headerMap;

};

} //namespace QCurl

#endif // QCNETWORKSYNCREPLY_H
