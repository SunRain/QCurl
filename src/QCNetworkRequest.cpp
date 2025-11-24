#include "QCNetworkRequest.h"

#include <QDebug>
#include <QSharedData>
#include <QUrl>
#include <QMap>

namespace QCurl {

typedef  QPair<QByteArray, QByteArray> RawHeaderPair;

class QCNetworkRequestPrivate : public QSharedData
{
public:
    QCNetworkRequestPrivate()
        : followLocation(true),
          reqUrl(QUrl()),
          rangeStart(-1),
          rangeEnd(-1)

    {

    }
    ~QCNetworkRequestPrivate() {}

    bool followLocation;
    int rangeStart;
    int rangeEnd;
    QMap<QByteArray, QByteArray> rawHeaderMap;
    QUrl reqUrl;
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
            && d.data()->reqUrl == other.d.data()->reqUrl
            && d.data()->rawHeaderMap == other.d.data()->rawHeaderMap
            && d.data()->rangeStart == other.d.data()->rangeStart
            && d.data()->rangeEnd == other.d.data()->rangeEnd;
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

void QCNetworkRequest::setRawHeader(const QByteArray &headerName, const QByteArray &headerValue)
{
    d.data()->rawHeaderMap.insert(headerName, headerValue);
}

QList<QByteArray> QCNetworkRequest::rawHeaderList() const
{
    QList<QByteArray> list;
    foreach (const QByteArray &key, d.data()->rawHeaderMap.keys()) {
        const QByteArray &value = d.data()->rawHeaderMap.value(key);
        QByteArray str(key);
        str += ": ";
        str += value;
        str.append(char(0));
        list.append(str);
    }
    return list;
}

QByteArray QCNetworkRequest::rawHeader(const QByteArray &headerName) const
{
    return d.data()->rawHeaderMap.value(headerName);
}

void QCNetworkRequest::setRange(int start, int end)
{
    d.data()->rangeStart = start;
    d.data()->rangeEnd = end;
}

int QCNetworkRequest::rangeStart() const
{
    return d.data()->rangeStart;
}

int QCNetworkRequest::rangeEnd() const
{
    return d.data()->rangeEnd;
}


QDebug operator <<(QDebug dbg, const QCNetworkRequest &req)
{
    return dbg<<QString("QCNetworkRequest [requestUrl=%1], [followLocation=%2], [range %3-%4]")
                .arg(req.url().toString())
                .arg(req.followLocation())
                .arg(req.rangeStart())
                .arg(req.rangeEnd());
    //TODO show headers
}














} //namespace QCurl


