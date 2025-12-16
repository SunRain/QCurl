// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkCancelToken.h"
#include "QCNetworkReply.h"
#include <QDebug>

namespace QCurl {

class QCNetworkCancelToken::Private
{
public:
    QList<QPointer<QCNetworkReply>> attachedReplies;
    QTimer *autoTimeoutTimer = nullptr;
    bool cancelled = false;
};

QCNetworkCancelToken::QCNetworkCancelToken(QObject *parent)
    : QObject(parent)
    , d_ptr(new Private)
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
    connect(reply, &QObject::destroyed, this, [this](QObject *obj) {
        detach(static_cast<QCNetworkReply *>(obj));
    });
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

    for (int i = d_ptr->attachedReplies.size() - 1; i >= 0; --i) {
        const QPointer<QCNetworkReply> &replyPtr = d_ptr->attachedReplies.at(i);
        if (!replyPtr || replyPtr.data() == reply) {
            d_ptr->attachedReplies.removeAt(i);
        }
    }

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
    const QList<QPointer<QCNetworkReply>> repliesToCancel = d_ptr->attachedReplies;
    for (const QPointer<QCNetworkReply> &replyPtr : repliesToCancel) {
        QCNetworkReply *reply = replyPtr.data();
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
    int count = 0;
    for (const QPointer<QCNetworkReply> &replyPtr : d_ptr->attachedReplies) {
        if (replyPtr) {
            ++count;
        }
    }
    return count;
}

bool QCNetworkCancelToken::isCancelled() const
{
    return d_ptr->cancelled;
}

void QCNetworkCancelToken::clear()
{
    for (const QPointer<QCNetworkReply> &replyPtr : d_ptr->attachedReplies) {
        QCNetworkReply *reply = replyPtr.data();
        if (reply) {
            disconnect(reply, nullptr, this, nullptr);
        }
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
