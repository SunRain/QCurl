// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkRequestBuilder.h"

#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"

#include <QIODevice>
#include <QPointer>
#include <QUrlQuery>

namespace QCurl {

class QCNetworkRequestBuilder::Private
{
public:
    QCNetworkAccessManager *manager = nullptr;
    QUrl url;
    QList<QPair<QString, QString>> headers;
    std::optional<QCNetworkHttpAuthConfig> httpAuthConfig;
    QList<QPair<QString, QString>> queryParams;
    QByteArray body;
    QPointer<QIODevice> uploadDevice;
    std::optional<QString> uploadFilePath;
    std::optional<qint64> uploadSizeBytes;
    int timeout         = -1;
    bool followLocation = true;
    QUrl proxy;
    bool sslVerify = true;
    QString caCert;
};

QCNetworkRequestBuilder::QCNetworkRequestBuilder(QCNetworkAccessManager *manager, const QUrl &url)
    : d_ptr(std::make_unique<Private>())
{
    d_ptr->manager = manager;
    d_ptr->url     = url;
}

QCNetworkRequestBuilder::QCNetworkRequestBuilder(QCNetworkRequestBuilder &&) noexcept = default;

QCNetworkRequestBuilder &QCNetworkRequestBuilder::operator=(QCNetworkRequestBuilder &&) noexcept
    = default;

QCNetworkRequestBuilder::~QCNetworkRequestBuilder() = default;

QCNetworkRequestBuilder &QCNetworkRequestBuilder::withHeader(const QString &name,
                                                             const QString &value)
{
    d_ptr->headers.append(qMakePair(name, value));
    return *this;
}

QCNetworkRequestBuilder &QCNetworkRequestBuilder::withBasicAuth(const QString &userName,
                                                                const QString &password)
{
    QCNetworkHttpAuthConfig cfg;
    cfg.userName          = userName;
    cfg.password          = password;
    cfg.method            = QCNetworkHttpAuthMethod::Basic;
    d_ptr->httpAuthConfig = cfg;
    return *this;
}

QCNetworkRequestBuilder &QCNetworkRequestBuilder::withHttpAuth(const QCNetworkHttpAuthConfig &config)
{
    d_ptr->httpAuthConfig = config;
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

QCNetworkRequestBuilder &QCNetworkRequestBuilder::withQueryParam(const QString &name,
                                                                 const QString &value)
{
    d_ptr->queryParams.append(qMakePair(name, value));
    return *this;
}

QCNetworkRequestBuilder &QCNetworkRequestBuilder::withBody(const QByteArray &body)
{
    d_ptr->body         = body;
    d_ptr->uploadDevice = nullptr;
    d_ptr->uploadFilePath.reset();
    d_ptr->uploadSizeBytes.reset();
    return *this;
}

QCNetworkRequestBuilder &QCNetworkRequestBuilder::withUploadDevice(QIODevice *device,
                                                                   std::optional<qint64> sizeBytes)
{
    d_ptr->uploadDevice = device;
    d_ptr->uploadFilePath.reset();
    d_ptr->uploadSizeBytes = sizeBytes;
    d_ptr->body.clear();
    return *this;
}

QCNetworkRequestBuilder &QCNetworkRequestBuilder::withUploadFile(const QString &filePath,
                                                                 std::optional<qint64> sizeBytes)
{
    d_ptr->uploadDevice    = nullptr;
    d_ptr->uploadFilePath  = filePath;
    d_ptr->uploadSizeBytes = sizeBytes;
    d_ptr->body.clear();
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
    if (d_ptr->httpAuthConfig.has_value()) {
        request.setHttpAuth(d_ptr->httpAuthConfig.value());
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
    if (d_ptr->httpAuthConfig.has_value()) {
        request.setHttpAuth(d_ptr->httpAuthConfig.value());
    }

    // Apply settings
    if (d_ptr->timeout > 0) {
        request.setTimeout(std::chrono::milliseconds(d_ptr->timeout));
    }
    request.setFollowLocation(d_ptr->followLocation);
    if (!d_ptr->proxy.isEmpty()) {
        // request.setProxyUrl(d_ptr->proxy);
    }

    if (d_ptr->uploadDevice || d_ptr->uploadFilePath.has_value()) {
        if (d_ptr->uploadDevice) {
            request.setUploadDevice(d_ptr->uploadDevice.data(), d_ptr->uploadSizeBytes);
        } else {
            request.setUploadFile(d_ptr->uploadFilePath.value(), d_ptr->uploadSizeBytes);
        }
        return d_ptr->manager->sendPost(request, QByteArray());
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
    if (d_ptr->httpAuthConfig.has_value()) {
        request.setHttpAuth(d_ptr->httpAuthConfig.value());
    }

    // Apply settings
    if (d_ptr->timeout > 0) {
        request.setTimeout(std::chrono::milliseconds(d_ptr->timeout));
    }
    request.setFollowLocation(d_ptr->followLocation);

    return d_ptr->manager->sendHead(request);
}

QCNetworkReply *QCNetworkRequestBuilder::sendDelete(const QByteArray &body)
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
    if (d_ptr->httpAuthConfig.has_value()) {
        request.setHttpAuth(d_ptr->httpAuthConfig.value());
    }

    // Apply settings
    if (d_ptr->timeout > 0) {
        request.setTimeout(std::chrono::milliseconds(d_ptr->timeout));
    }

    QByteArray finalBody = body.isEmpty() ? d_ptr->body : body;
    return d_ptr->manager->sendDelete(request, finalBody);
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
    if (d_ptr->httpAuthConfig.has_value()) {
        request.setHttpAuth(d_ptr->httpAuthConfig.value());
    }

    // Apply settings
    if (d_ptr->timeout > 0) {
        request.setTimeout(std::chrono::milliseconds(d_ptr->timeout));
    }

    if (d_ptr->uploadDevice || d_ptr->uploadFilePath.has_value()) {
        if (d_ptr->uploadDevice) {
            request.setUploadDevice(d_ptr->uploadDevice.data(), d_ptr->uploadSizeBytes);
        } else {
            request.setUploadFile(d_ptr->uploadFilePath.value(), d_ptr->uploadSizeBytes);
        }
        return d_ptr->manager->sendPut(request, QByteArray());
    }

    QByteArray finalBody = body.isEmpty() ? d_ptr->body : body;
    return d_ptr->manager->sendPut(request, finalBody);
}

} // namespace QCurl
