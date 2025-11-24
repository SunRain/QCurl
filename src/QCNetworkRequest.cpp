#include "QCNetworkRequest.h"

#include <QDebug>
#include <QSharedData>
#include <QUrl>

namespace QCurl {

class QCNetworkRequestPrivate : public QSharedData
{
public:
    QCNetworkRequestPrivate()
        : reqUrl(QUrl()),
          followLocation(true)
    {

    }
    virtual ~QCNetworkRequestPrivate() {}

    QUrl reqUrl;
    bool followLocation;
};

QCNetworkRequest::QCNetworkRequest()
    : d(new QCNetworkRequestPrivate())
{

}

QCNetworkRequest::QCNetworkRequest(const QUrl &url)
    : d(new QCNetworkRequestPrivate())
{
    d.data()->reqUrl = url;
}

QCNetworkRequest::QCNetworkRequest(const QCNetworkRequest &other)
    : d(other.d)
{

}

QCNetworkRequest::~QCNetworkRequest()
{

}

QCNetworkRequest &QCNetworkRequest::operator =(const QCNetworkRequest &other)
{
    if (this != &other)
        d.operator =(other.d);
    return *this;
}

bool QCNetworkRequest::operator ==(const QCNetworkRequest &other)
{
    return d.data()->followLocation == other.d.data()->followLocation
            && d.data()->reqUrl == other.d.data()->reqUrl;
}

bool QCNetworkRequest::operator !=(const QCNetworkRequest &other)
{
    return !operator==(other);
}

QUrl QCNetworkRequest::url() const
{
    return d.data()->reqUrl;
}

void QCNetworkRequest::setFollowLocation(bool followLocation)
{
    d.data()->followLocation = followLocation;
}

bool QCNetworkRequest::followLocation() const
{
    return d.data()->followLocation;
}

QDebug operator <<(QDebug dbg, const QCNetworkRequest &req)
{
    return dbg<<QString("QCNetworkRequest [requestUrl=%1], [followLocation=%2]")
                .arg(req.url().toString())
                .arg(req.followLocation());
}














} //namespace QCurl


