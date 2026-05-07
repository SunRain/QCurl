// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkMockHandler.h"

#include "QCNetworkHttpMethod.h"

#include <QSharedData>

namespace QCurl {

/// 请求捕获快照的共享存储，仅保存离线断言需要的归一化字段。
class QCNetworkCapturedRequestData : public QSharedData
{
public:
    QUrl url;
    HttpMethod method = HttpMethod::Get;
    QList<QCNetworkCapturedRequest::RawHeaderPair> headers;
    QByteArray bodyPreview;
    qsizetype bodySize = 0;
    bool followLocation = true;
    // 空值表示请求未显式配置对应 timeout。
    std::optional<qint64> connectTimeoutMs;
    std::optional<qint64> totalTimeoutMs;
};

QCNetworkCapturedRequest::QCNetworkCapturedRequest()
    : d(new QCNetworkCapturedRequestData)
{}

QCNetworkCapturedRequest::QCNetworkCapturedRequest(
    const QCNetworkCapturedRequest &other) = default;

QCNetworkCapturedRequest::QCNetworkCapturedRequest(
    QCNetworkCapturedRequest &&other) noexcept = default;

QCNetworkCapturedRequest::~QCNetworkCapturedRequest() = default;

QCNetworkCapturedRequest &QCNetworkCapturedRequest::operator=(
    const QCNetworkCapturedRequest &other) = default;

QCNetworkCapturedRequest &QCNetworkCapturedRequest::operator=(
    QCNetworkCapturedRequest &&other) noexcept = default;

QUrl QCNetworkCapturedRequest::url() const
{
    return d->url;
}

void QCNetworkCapturedRequest::setUrl(const QUrl &url)
{
    d->url = url;
}

HttpMethod QCNetworkCapturedRequest::method() const
{
    return d->method;
}

void QCNetworkCapturedRequest::setMethod(HttpMethod method)
{
    d->method = method;
}

QList<QCNetworkCapturedRequest::RawHeaderPair> QCNetworkCapturedRequest::headers() const
{
    return d->headers;
}

void QCNetworkCapturedRequest::setHeaders(const QList<RawHeaderPair> &headers)
{
    d->headers = headers;
}

void QCNetworkCapturedRequest::addHeader(const QByteArray &name, const QByteArray &value)
{
    d->headers.append(qMakePair(name, value));
}

QByteArray QCNetworkCapturedRequest::bodyPreview() const
{
    return d->bodyPreview;
}

void QCNetworkCapturedRequest::setBodyPreview(const QByteArray &bodyPreview)
{
    d->bodyPreview = bodyPreview;
}

qsizetype QCNetworkCapturedRequest::bodySize() const
{
    return d->bodySize;
}

void QCNetworkCapturedRequest::setBodySize(qsizetype bodySize)
{
    d->bodySize = bodySize;
}

bool QCNetworkCapturedRequest::followLocation() const
{
    return d->followLocation;
}

void QCNetworkCapturedRequest::setFollowLocation(bool followLocation)
{
    d->followLocation = followLocation;
}

std::optional<qint64> QCNetworkCapturedRequest::connectTimeoutMs() const
{
    return d->connectTimeoutMs;
}

void QCNetworkCapturedRequest::setConnectTimeoutMs(qint64 timeoutMs)
{
    d->connectTimeoutMs = timeoutMs;
}

void QCNetworkCapturedRequest::clearConnectTimeoutMs()
{
    d->connectTimeoutMs.reset();
}

std::optional<qint64> QCNetworkCapturedRequest::totalTimeoutMs() const
{
    return d->totalTimeoutMs;
}

void QCNetworkCapturedRequest::setTotalTimeoutMs(qint64 timeoutMs)
{
    d->totalTimeoutMs = timeoutMs;
}

void QCNetworkCapturedRequest::clearTotalTimeoutMs()
{
    d->totalTimeoutMs.reset();
}

} // namespace QCurl
