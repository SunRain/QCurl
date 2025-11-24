#ifndef QCNETWORKREQUEST_H
#define QCNETWORKREQUEST_H

#include <QString>
#include <QSharedDataPointer>
#include <QList>
#include <QByteArray>

#include <QUrl>

namespace QCurl {

class QCNetworkRequestPrivate;
class QCNetworkRequest
{
public:
    QCNetworkRequest();
    QCNetworkRequest(const QUrl &url);
    QCNetworkRequest(const QCNetworkRequest &other);
    virtual ~QCNetworkRequest();

    QCNetworkRequest &operator =(const QCNetworkRequest &other);
    bool operator ==(const QCNetworkRequest &other);
    bool operator !=(const QCNetworkRequest &other);

    QUrl url() const;

    ///
    /// \brief setFollowLocation Follow redirects
    /// \param followLocation default true
    ///
    void setFollowLocation(bool followLocation = true);

    bool followLocation() const;

    ///
    /// \brief setRawHeader
    /// \param headerName
    /// \param headerValue
    /// Setting the same header twice overrides the previous setting
    void setRawHeader(const QByteArray &headerName, const QByteArray &headerValue);

    ///
    /// \brief rawHeaderList
    /// \return
    /// Returns a list of all raw headers that are set in this network request.
    /// The list is in the order that the headers were set.
    QList<QByteArray> rawHeaderList() const;

    QByteArray rawHeader(const QByteArray &headerName) const;

    void setRange(int start, int end);

    int rangeStart() const;

    int rangeEnd() const;

private:
    QSharedDataPointer<QCurl::QCNetworkRequestPrivate> d;

};

QDebug operator <<(QDebug dbg, const QCNetworkRequest &req);


} //namespace QCurl
#endif // QCNETWORKREQUEST_H
