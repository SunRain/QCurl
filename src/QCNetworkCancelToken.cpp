// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkCancelToken.h"
#include "QCNetworkReply.h"
#include <QDebug>

namespace QCurl {

class QCNetworkCancelToken::Private
{
public:
    QList<QCNetworkReply *> attachedReplies;
    QTimer *autoTimeoutTimer = nullptr;
    bool cancelled = false;
};

QCNetworkCancelToken::QCNetworkCancelToken(QObject *parent)
    : QObject(parent)
    , d_ptr(std::make_unique<Private>())
{
}

QCNetworkCancelToken::~QCNetworkCancelToken()
{
    cancel();
}

void QCNetworkCancelToken::attach(QCNetworkReply *reply)
{
    if (!reply || d_ptr->attachedReplies.contains(reply)) {
        return;
    }
    
    d_ptr->attachedReplies.append(reply);
    
    // Connect to finished signal
    connect(reply, &QCNetworkReply::finished, this, &QCNetworkCancelToken::onReplyFinished);
}

void QCNetworkCancelToken::attachMultiple(const QList<QCNetworkReply *> &replies)
{
    for (auto *reply : replies) {
        attach(reply);
    }
}

void QCNetworkCancelToken::detach(QCNetworkReply *reply)
{
    if (!reply) {
        return;
    }
    
    d_ptr->attachedReplies.removeAll(reply);
    disconnect(reply, nullptr, this, nullptr);
}

void QCNetworkCancelToken::cancel()
{
    if (d_ptr->cancelled) {
        return;
    }
    
    d_ptr->cancelled = true;
    
    // Stop auto timeout timer
    if (d_ptr->autoTimeoutTimer) {
        d_ptr->autoTimeoutTimer->stop();
        d_ptr->autoTimeoutTimer->deleteLater();
        d_ptr->autoTimeoutTimer = nullptr;
    }
    
    // Cancel all attached replies
    QList<QCNetworkReply *> repliesToCancel = d_ptr->attachedReplies;
    for (auto *reply : repliesToCancel) {
        if (reply) {
            reply->cancel();
            detach(reply);
        }
    }
    
    d_ptr->attachedReplies.clear();
    
    emit cancelled();
}

void QCNetworkCancelToken::setAutoTimeout(int msecs)
{
    if (msecs <= 0) {
        if (d_ptr->autoTimeoutTimer) {
            d_ptr->autoTimeoutTimer->stop();
            d_ptr->autoTimeoutTimer->deleteLater();
            d_ptr->autoTimeoutTimer = nullptr;
        }
        return;
    }
    
    if (!d_ptr->autoTimeoutTimer) {
        d_ptr->autoTimeoutTimer = new QTimer(this);
        connect(d_ptr->autoTimeoutTimer, &QTimer::timeout,
                this, &QCNetworkCancelToken::onAutoTimeoutTriggered);
    }
    
    d_ptr->autoTimeoutTimer->start(msecs);
}

int QCNetworkCancelToken::attachedCount() const
{
    return d_ptr->attachedReplies.size();
}

bool QCNetworkCancelToken::isCancelled() const
{
    return d_ptr->cancelled;
}

void QCNetworkCancelToken::clear()
{
    for (auto *reply : d_ptr->attachedReplies) {
        disconnect(reply, nullptr, this, nullptr);
    }
    d_ptr->attachedReplies.clear();
}

void QCNetworkCancelToken::onReplyFinished()
{
    auto *reply = qobject_cast<QCNetworkReply *>(sender());
    if (reply) {
        emit requestCompleted(reply);
        detach(reply);
    }
}

void QCNetworkCancelToken::onAutoTimeoutTriggered()
{
    if (!d_ptr->cancelled) {
        cancel();
    }
}

} // namespace QCurl
