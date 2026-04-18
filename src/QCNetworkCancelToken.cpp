// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkCancelToken.h"

#include "QCNetworkReply.h"

#include <QDebug>

namespace QCurl {

class QCNetworkCancelTokenPrivate
{
public:
    QList<QPointer<QCNetworkReply>> attachedReplies;
    QTimer *autoTimeoutTimer = nullptr;
    bool cancelled           = false;
};

QCNetworkCancelToken::QCNetworkCancelToken(QObject *parent)
    : QObject(parent)
    , d_ptr(new QCNetworkCancelTokenPrivate)
{}

QCNetworkCancelToken::~QCNetworkCancelToken()
{
    cancel();
}

void QCNetworkCancelToken::attach(QCNetworkReply *reply)
{
    Q_D(QCNetworkCancelToken);

    if (!reply || d->attachedReplies.contains(reply)) {
        return;
    }

    d->attachedReplies.append(reply);

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
    Q_D(QCNetworkCancelToken);

    if (!reply) {
        return;
    }

    for (int i = d->attachedReplies.size() - 1; i >= 0; --i) {
        const QPointer<QCNetworkReply> &replyPtr = d->attachedReplies.at(i);
        if (!replyPtr || replyPtr.data() == reply) {
            d->attachedReplies.removeAt(i);
        }
    }

    disconnect(reply, nullptr, this, nullptr);
}

void QCNetworkCancelToken::cancel()
{
    Q_D(QCNetworkCancelToken);

    if (d->cancelled) {
        return;
    }

    d->cancelled = true;

    if (d->autoTimeoutTimer) {
        d->autoTimeoutTimer->stop();
        d->autoTimeoutTimer->deleteLater();
        d->autoTimeoutTimer = nullptr;
    }

    const QList<QPointer<QCNetworkReply>> repliesToCancel = d->attachedReplies;
    for (const QPointer<QCNetworkReply> &replyPtr : repliesToCancel) {
        QCNetworkReply *reply = replyPtr.data();
        if (reply) {
            reply->cancel();
            detach(reply);
        }
    }

    d->attachedReplies.clear();

    emit cancelled();
}

void QCNetworkCancelToken::setAutoTimeout(int msecs)
{
    Q_D(QCNetworkCancelToken);

    if (msecs <= 0) {
        if (d->autoTimeoutTimer) {
            d->autoTimeoutTimer->stop();
            d->autoTimeoutTimer->deleteLater();
            d->autoTimeoutTimer = nullptr;
        }
        return;
    }

    if (!d->autoTimeoutTimer) {
        d->autoTimeoutTimer = new QTimer(this);
        connect(d->autoTimeoutTimer,
                &QTimer::timeout,
                this,
                &QCNetworkCancelToken::onAutoTimeoutTriggered);
    }

    d->autoTimeoutTimer->start(msecs);
}

int QCNetworkCancelToken::attachedCount() const
{
    Q_D(const QCNetworkCancelToken);

    int count = 0;
    for (const QPointer<QCNetworkReply> &replyPtr : d->attachedReplies) {
        if (replyPtr) {
            ++count;
        }
    }
    return count;
}

bool QCNetworkCancelToken::isCancelled() const
{
    Q_D(const QCNetworkCancelToken);
    return d->cancelled;
}

void QCNetworkCancelToken::clear()
{
    Q_D(QCNetworkCancelToken);

    for (const QPointer<QCNetworkReply> &replyPtr : d->attachedReplies) {
        QCNetworkReply *reply = replyPtr.data();
        if (reply) {
            disconnect(reply, nullptr, this, nullptr);
        }
    }
    d->attachedReplies.clear();
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
    Q_D(QCNetworkCancelToken);
    if (!d->cancelled) {
        cancel();
    }
}

} // namespace QCurl
