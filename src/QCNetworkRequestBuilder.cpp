// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkRequestBuilder.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"
#include <QUrlQuery>

namespace QCurl {

class QCNetworkRequestBuilder::Private
{
public:
    QCNetworkAccessManager *manager = nullptr;
    QUrl url;
    QList<QPair<QString, QString>> headers;
    QList<QPair<QString, QString>> queryParams;
    QByteArray body;
    int timeout = -1;
    bool followLocation = false;
    QUrl proxy;
    bool sslVerify = true;
    QString caCert;
};

QCNetworkRequestBuilder::QCNetworkRequestBuilder(QCNetworkAccessManager *manager, const QUrl &url)
    : d_ptr(std::make_unique<Private>())
{
    d_ptr->manager = manager;
    d_ptr->url = url;
}

QCNetworkRequestBuilder::QCNetworkRequestBuilder(QCNetworkRequestBuilder &&) noexcept = default;

QCNetworkRequestBuilder &QCNetworkRequestBuilder::operator=(QCNetworkRequestBuilder &&) noexcept = default;

QCNetworkRequestBuilder::~QCNetworkRequestBuilder() = default;

QCNetworkRequestBuilder &QCNetworkRequestBuilder::withHeader(const QString &name, const QString &value)
{
    d_ptr->headers.append(qMakePair(name, value));
    return *this;
}

QCNetworkRequestBuilder &QCNetworkRequestBuilder::withContentType(const QString &contentType)
{
    return withHeader("Content-Type", contentType);
}

QCNetworkRequestBuilder &QCNetworkRequestBuilder::withUserAgent(const QString &userAgent)
{
    return withHeader("User-Agent", userAgent);
}

QCNetworkRequestBuilder &QCNetworkRequestBuilder::withTimeout(int msecs)
{
    d_ptr->timeout = msecs;
    return *this;
}

QCNetworkRequestBuilder &QCNetworkRequestBuilder::withFollowLocation(bool follow)
{
    d_ptr->followLocation = follow;
    return *this;
}

QCNetworkRequestBuilder &QCNetworkRequestBuilder::withProxy(const QUrl &proxyUrl)
{
    d_ptr->proxy = proxyUrl;
    return *this;
}

QCNetworkRequestBuilder &QCNetworkRequestBuilder::withSSLVerify(bool verify)
{
    d_ptr->sslVerify = verify;
    return *this;
}

QCNetworkRequestBuilder &QCNetworkRequestBuilder::withCACert(const QString &certPath)
{
    d_ptr->caCert = certPath;
    return *this;
}

QCNetworkRequestBuilder &QCNetworkRequestBuilder::withQueryParam(const QString &name, const QString &value)
{
    d_ptr->queryParams.append(qMakePair(name, value));
    return *this;
}

QCNetworkRequestBuilder &QCNetworkRequestBuilder::withBody(const QByteArray &body)
{
    d_ptr->body = body;
    return *this;
}

QCNetworkReply *QCNetworkRequestBuilder::sendGet()
{
    QUrl finalUrl = d_ptr->url;
    
    // Add query parameters if any
    if (!d_ptr->queryParams.isEmpty()) {
        QUrlQuery query(finalUrl);
        for (const auto &param : d_ptr->queryParams) {
            query.addQueryItem(param.first, param.second);
        }
        finalUrl.setQuery(query);
    }
    
    QCNetworkRequest request(finalUrl);
    
    // Apply headers
    for (const auto &header : d_ptr->headers) {
        request.setRawHeader(header.first.toLatin1(), header.second.toLatin1());
    }
    
    // Apply settings
    if (d_ptr->timeout > 0) {
        request.setTimeout(std::chrono::milliseconds(d_ptr->timeout));
    }
    request.setFollowLocation(d_ptr->followLocation);
    if (!d_ptr->proxy.isEmpty()) {
        // request.setProxyUrl(d_ptr->proxy);
    }
    
    return d_ptr->manager->sendGet(request);
}

QCNetworkReply *QCNetworkRequestBuilder::sendPost(const QByteArray &body)
{
    QUrl finalUrl = d_ptr->url;
    
    // Add query parameters if any
    if (!d_ptr->queryParams.isEmpty()) {
        QUrlQuery query(finalUrl);
        for (const auto &param : d_ptr->queryParams) {
            query.addQueryItem(param.first, param.second);
        }
        finalUrl.setQuery(query);
    }
    
    QCNetworkRequest request(finalUrl);
    
    // Apply headers
    for (const auto &header : d_ptr->headers) {
        request.setRawHeader(header.first.toLatin1(), header.second.toLatin1());
    }
    
    // Apply settings
    if (d_ptr->timeout > 0) {
        request.setTimeout(std::chrono::milliseconds(d_ptr->timeout));
    }
    request.setFollowLocation(d_ptr->followLocation);
    if (!d_ptr->proxy.isEmpty()) {
        // request.setProxyUrl(d_ptr->proxy);
    }
    
    QByteArray finalBody = body.isEmpty() ? d_ptr->body : body;
    return d_ptr->manager->sendPost(request, finalBody);
}

QCNetworkReply *QCNetworkRequestBuilder::sendHead()
{
    QUrl finalUrl = d_ptr->url;
    
    // Add query parameters if any
    if (!d_ptr->queryParams.isEmpty()) {
        QUrlQuery query(finalUrl);
        for (const auto &param : d_ptr->queryParams) {
            query.addQueryItem(param.first, param.second);
        }
        finalUrl.setQuery(query);
    }
    
    QCNetworkRequest request(finalUrl);
    
    // Apply headers
    for (const auto &header : d_ptr->headers) {
        request.setRawHeader(header.first.toLatin1(), header.second.toLatin1());
    }
    
    // Apply settings
    if (d_ptr->timeout > 0) {
        request.setTimeout(std::chrono::milliseconds(d_ptr->timeout));
    }
    request.setFollowLocation(d_ptr->followLocation);
    
    return d_ptr->manager->sendHead(request);
}

QCNetworkReply *QCNetworkRequestBuilder::sendDelete()
{
    QUrl finalUrl = d_ptr->url;
    
    // Add query parameters if any
    if (!d_ptr->queryParams.isEmpty()) {
        QUrlQuery query(finalUrl);
        for (const auto &param : d_ptr->queryParams) {
            query.addQueryItem(param.first, param.second);
        }
        finalUrl.setQuery(query);
    }
    
    QCNetworkRequest request(finalUrl);
    
    // Apply headers
    for (const auto &header : d_ptr->headers) {
        request.setRawHeader(header.first.toLatin1(), header.second.toLatin1());
    }
    
    // Apply settings
    if (d_ptr->timeout > 0) {
        request.setTimeout(std::chrono::milliseconds(d_ptr->timeout));
    }
    
    return d_ptr->manager->sendDelete(request);
}

QCNetworkReply *QCNetworkRequestBuilder::sendPut(const QByteArray &body)
{
    QUrl finalUrl = d_ptr->url;
    
    // Add query parameters if any
    if (!d_ptr->queryParams.isEmpty()) {
        QUrlQuery query(finalUrl);
        for (const auto &param : d_ptr->queryParams) {
            query.addQueryItem(param.first, param.second);
        }
        finalUrl.setQuery(query);
    }
    
    QCNetworkRequest request(finalUrl);
    
    // Apply headers
    for (const auto &header : d_ptr->headers) {
        request.setRawHeader(header.first.toLatin1(), header.second.toLatin1());
    }
    
    // Apply settings
    if (d_ptr->timeout > 0) {
        request.setTimeout(std::chrono::milliseconds(d_ptr->timeout));
    }
    
    QByteArray finalBody = body.isEmpty() ? d_ptr->body : body;
    return d_ptr->manager->sendPut(request, finalBody);
}

} // namespace QCurl
