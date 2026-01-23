// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkMiddleware.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkLogger.h"
#include "QCNetworkLogRedaction.h"
#include "QCNetworkRetryPolicy.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

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
    }
    return QStringLiteral("UNKNOWN");
}

QCNetworkLogger *loggerFromReply(QCNetworkReply *reply)
{
    auto *manager = reply ? qobject_cast<QCNetworkAccessManager*>(reply->parent()) : nullptr;
    return manager ? manager->logger() : nullptr;
}

constexpr const char kObservabilityRetryCountProperty[] = "_qcurl_observe_retry_count";

} // namespace

// ============================================================================
// QCLoggingMiddleware Implementation
// ============================================================================

void QCLoggingMiddleware::onRequestPreSend(QCNetworkRequest &request)
{
    qDebug() << "[QCurl Middleware] Sending request:"
             << request.url().toString();
}

void QCLoggingMiddleware::onResponseReceived(QCNetworkReply *reply)
{
    if (!reply) {
        return;
    }
    
    if (reply->error() == NetworkError::NoError) {
        qDebug() << "[QCurl Middleware] Response received:"
                 << reply->url().toString();
    } else {
        qWarning() << "[QCurl Middleware] Response error:"
                   << reply->url().toString()
                   << "Error:" << reply->errorString();
    }
}

// ============================================================================
// QCErrorHandlingMiddleware Implementation
// ============================================================================

void QCErrorHandlingMiddleware::setErrorCallback(std::function<void(const QString &)> callback)
{
    m_errorCallback = callback;
}

void QCErrorHandlingMiddleware::onResponseReceived(QCNetworkReply *reply)
{
    if (!reply) {
        return;
    }
    
    if (reply->error() != NetworkError::NoError) {
        QString errorMsg = reply->errorString();
        
        if (m_errorCallback) {
            m_errorCallback(errorMsg);
        }
        
        qWarning() << "[QCurl] Error:" << errorMsg;
    }
}

// ============================================================================
// QCSigningMiddleware Implementation
// ============================================================================

void QCSigningMiddleware::setSigningKey(const QString &key)
{
    m_signingKey = key;
}

void QCSigningMiddleware::onRequestPreSend(QCNetworkRequest &request)
{
    if (m_signingKey.isEmpty()) {
        return;
    }
    
    // Generate signature (simple example: SHA256 hash of key + timestamp)
    QString timestamp = QString::number(QDateTime::currentMSecsSinceEpoch());
    QString signString = m_signingKey + timestamp;
    
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(signString.toUtf8());
    QString signature = QString::fromLatin1(hash.result().toHex());
    
    // Add signature headers
    request.setRawHeader("X-Signature", signature.toLatin1());
    request.setRawHeader("X-Timestamp", timestamp.toLatin1());
}

// ============================================================================
// QCUnifiedRetryPolicyMiddleware Implementation
// ============================================================================

QCUnifiedRetryPolicyMiddleware::QCUnifiedRetryPolicyMiddleware(const QCNetworkRetryPolicy &defaultPolicy)
    : m_defaultPolicy(defaultPolicy)
{
}

void QCUnifiedRetryPolicyMiddleware::setDefaultPolicy(const QCNetworkRetryPolicy &policy)
{
    m_defaultPolicy = policy;
}

QCNetworkRetryPolicy QCUnifiedRetryPolicyMiddleware::defaultPolicy() const
{
    return m_defaultPolicy;
}

void QCUnifiedRetryPolicyMiddleware::onRequestPreSend(QCNetworkRequest &request)
{
    if (request.isRetryPolicyExplicit()) {
        return;
    }

    request.setRetryPolicy(m_defaultPolicy);
}

// ============================================================================
// QCRedactingLoggingMiddleware Implementation
// ============================================================================

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
    const QString url = QCNetworkLogRedaction::redactUrl(reply->url());

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
    const QString url = QCNetworkLogRedaction::redactUrl(reply->url());

    const int httpStatus = reply->httpStatusCode();
    const qint64 duration = reply->durationMs();
    const int errorCode = static_cast<int>(reply->error());

    const NetworkLogLevel level =
        (reply->error() == NetworkError::NoError) ? NetworkLogLevel::Info : NetworkLogLevel::Warning;

    logger->log(level,
                QStringLiteral("Response"),
                QStringLiteral("method=%1 url=%2 status=%3 durationMs=%4 error=%5")
                    .arg(method, url)
                    .arg(httpStatus)
                    .arg(duration)
                    .arg(errorCode));
}

// ============================================================================
// QCObservabilityMiddleware Implementation
// ============================================================================

void QCObservabilityMiddleware::onReplyCreated(QCNetworkReply *reply)
{
    if (!reply) {
        return;
    }

    if (reply->property(kObservabilityRetryCountProperty).isValid()) {
        return;
    }

    reply->setProperty(kObservabilityRetryCountProperty, 0);
    QObject::connect(reply, &QCNetworkReply::retryAttempt, reply, [reply](int attemptCount, NetworkError) {
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

    const QString url = QCNetworkLogRedaction::redactUrl(reply->url());
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
