// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkMiddleware.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"
#include "QCNetworkLogger.h"
#include <QCryptographicHash>
#include <QDebug>

namespace QCurl {

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
    if (!reply) return;
    
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
    if (!reply) return;
    
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

} // namespace QCurl
