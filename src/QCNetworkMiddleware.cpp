// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkMiddleware.h"

#include "QCNetworkAccessManager.h"
#include "private/QCNetworkMiddlewareInternal_p.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QSet>

#include <functional>

namespace QCurl {

/// 记录借用本中间件的 manager；析构时逐个解除注册关系。
class QCNetworkMiddlewarePrivate
{
public:
    QSet<QCNetworkAccessManager *> registeredManagers;
};

QCNetworkMiddleware::QCNetworkMiddleware()
    : d_ptr(new QCNetworkMiddlewarePrivate)
{}

QCNetworkMiddleware::~QCNetworkMiddleware()
{
    const auto managers = d_ptr->registeredManagers.values();
    for (auto *manager : managers) {
        if (manager) {
            manager->removeMiddleware(this);
        }
    }
}

void QCNetworkMiddleware::registerManager(QCNetworkAccessManager *manager)
{
    if (manager) {
        d_ptr->registeredManagers.insert(manager);
    }
}

void QCNetworkMiddleware::unregisterManager(QCNetworkAccessManager *manager)
{
    d_ptr->registeredManagers.remove(manager);
}

// ==================
// QCLoggingMiddleware Implementation
// ==================

void QCLoggingMiddleware::onRequestPreSend(QCNetworkRequest &request)
{
    qDebug() << "[QCurl Middleware] Sending request:" << request.url().toString();
}

void QCLoggingMiddleware::onResponseReceived(QCNetworkReply *reply)
{
    if (!reply) {
        return;
    }

    if (reply->error() == NetworkError::NoError) {
        qDebug() << "[QCurl Middleware] Response received:" << reply->url().toString();
    } else {
        qWarning() << "[QCurl Middleware] Response error:" << reply->url().toString()
                   << "Error:" << reply->errorString();
    }
}

// ==================
// QCErrorHandlingMiddleware Implementation
// ==================

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

// ==================
// QCSigningMiddleware Implementation
// ==================

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
    QString timestamp  = QString::number(QDateTime::currentMSecsSinceEpoch());
    QString signString = m_signingKey + timestamp;

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(signString.toUtf8());
    QString signature = QString::fromLatin1(hash.result().toHex());

    // Add signature headers
    request.setRawHeader("X-Signature", signature.toLatin1());
    request.setRawHeader("X-Timestamp", timestamp.toLatin1());
}

// ==================
// QCUnifiedRetryPolicyMiddleware Implementation
// ==================

QCUnifiedRetryPolicyMiddleware::QCUnifiedRetryPolicyMiddleware(
    const QCNetworkRetryPolicy &defaultPolicy)
    : m_defaultPolicy(defaultPolicy)
{}

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

} // namespace QCurl
