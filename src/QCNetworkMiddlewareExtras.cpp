// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkMiddlewareExtras.h"

#include "QCNetworkAccessManager.h"
#include "QCNetworkLogger.h"
#include "QCNetworkReply.h"
#include "private/QCNetworkLogRedaction_p.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace QCurl {

namespace {

QString httpMethodToString(HttpMethod method)
{
    switch (method) {
        case HttpMethod::Head:
            return QStringLiteral("HEAD");
        case HttpMethod::Get:
            return QStringLiteral("GET");
        case HttpMethod::Post:
            return QStringLiteral("POST");
        case HttpMethod::Put:
            return QStringLiteral("PUT");
        case HttpMethod::Delete:
            return QStringLiteral("DELETE");
        case HttpMethod::Patch:
            return QStringLiteral("PATCH");
        case HttpMethod::Custom:
            return QStringLiteral("CUSTOM");
    }
    return QStringLiteral("UNKNOWN");
}

QCNetworkLogger *loggerFromReply(QCNetworkReply *reply)
{
    auto *manager = reply ? qobject_cast<QCNetworkAccessManager *>(reply->parent()) : nullptr;
    return manager ? manager->logger() : nullptr;
}

constexpr const char kObservabilityRetryCountProperty[] = "_qcurl_observe_retry_count";

} // namespace

void QCRedactingLoggingMiddleware::onReplyCreated(QCNetworkReply *reply)
{
    if (!reply) {
        return;
    }

    auto *logger = loggerFromReply(reply);
    if (!logger) {
        return;
    }

    const QString method = httpMethodToString(reply->method());
    const QString url    = QCNetworkLogRedaction::redactUrl(reply->url());

    logger->log(NetworkLogLevel::Debug,
                QStringLiteral("Request"),
                QStringLiteral("method=%1 url=%2").arg(method, url));
}

void QCRedactingLoggingMiddleware::onResponseReceived(QCNetworkReply *reply)
{
    if (!reply) {
        return;
    }

    auto *logger = loggerFromReply(reply);
    if (!logger) {
        return;
    }

    const QString method = httpMethodToString(reply->method());
    const QString url    = QCNetworkLogRedaction::redactUrl(reply->url());

    const int httpStatus  = reply->httpStatusCode();
    const qint64 duration = reply->durationMs();
    const int errorCode   = static_cast<int>(reply->error());

    const NetworkLogLevel level = (reply->error() == NetworkError::NoError)
                                      ? NetworkLogLevel::Info
                                      : NetworkLogLevel::Warning;

    logger->log(level,
                QStringLiteral("Response"),
                QStringLiteral("method=%1 url=%2 status=%3 durationMs=%4 error=%5")
                    .arg(method, url)
                    .arg(httpStatus)
                    .arg(duration)
                    .arg(errorCode));
}

void QCObservabilityMiddleware::onReplyCreated(QCNetworkReply *reply)
{
    if (!reply) {
        return;
    }

    if (reply->property(kObservabilityRetryCountProperty).isValid()) {
        return;
    }

    reply->setProperty(kObservabilityRetryCountProperty, 0);
    QObject::connect(reply,
                     &QCNetworkReply::retryAttempt,
                     reply,
                     [reply](int attemptCount, NetworkError) {
                         reply->setProperty(kObservabilityRetryCountProperty, attemptCount);
                     });
}

void QCObservabilityMiddleware::onResponseReceived(QCNetworkReply *reply)
{
    if (!reply) {
        return;
    }

    auto *logger = loggerFromReply(reply);
    if (!logger) {
        return;
    }

    const QString url    = QCNetworkLogRedaction::redactUrl(reply->url());
    const QString method = httpMethodToString(reply->method());

    const int retryCount = reply->property(kObservabilityRetryCountProperty).toInt();

    QJsonObject obj;
    obj.insert(QStringLiteral("url"), url);
    obj.insert(QStringLiteral("method"), method);
    obj.insert(QStringLiteral("httpStatusCode"), reply->httpStatusCode());
    obj.insert(QStringLiteral("durationMs"), static_cast<double>(reply->durationMs()));
    obj.insert(QStringLiteral("attemptCount"), retryCount);
    obj.insert(QStringLiteral("bytesReceived"), static_cast<double>(reply->bytesReceived()));
    obj.insert(QStringLiteral("bytesTotal"), static_cast<double>(reply->bytesTotal()));
    obj.insert(QStringLiteral("error"), static_cast<int>(reply->error()));

    logger->log(NetworkLogLevel::Info,
                QStringLiteral("Observability"),
                QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

} // namespace QCurl
