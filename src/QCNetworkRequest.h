#ifndef QCNETWORKREQUEST_H
#define QCNETWORKREQUEST_H

#include <QString>
#include <QSharedDataPointer>

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

private:
    QSharedDataPointer<QCurl::QCNetworkRequestPrivate> d;

};

QDebug operator <<(QDebug dbg, const QCNetworkRequest &req);


} //namespace QCurl
#endif // QCNETWORKREQUEST_H
