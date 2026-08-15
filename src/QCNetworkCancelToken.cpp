// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkCancelToken.h"

#include "QCNetworkReply.h"

#include <QDebug>
#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <QSignalBlocker>
#include <QThread>
#include <QTimer>

#include <optional>

namespace QCurl {

/**
 * @brief 保存取消令牌的同线程 reply registration 和自动超时状态。
 *
 * `QPointer` 只在 token/reply 共同 owner thread 判空和解引用；raw identity 仅作为哈希键，
 * destroyed 清理从不解引用或断开已经析构的对象地址。
 */
class QCNetworkCancelTokenPrivate
{
public:
    struct ReplyRegistration
    {
        quint64 id               = 0;
        QCNetworkReply *identity = nullptr;
        QPointer<QCNetworkReply> reply;
        QMetaObject::Connection finishedConnection;
        QMetaObject::Connection destroyedConnection;
    };

    explicit QCNetworkCancelTokenPrivate(QCNetworkCancelToken *q)
        : q_ptr(q)
    {}

    [[nodiscard]] bool isOwnerThread() const noexcept
    {
        return QThread::currentThread() == q_ptr->thread();
    }

    [[nodiscard]] std::optional<ReplyRegistration> takeRegistration(quint64 id,
                                                                    bool disconnectConnections)
    {
        auto it = registrations.find(id);
        if (it == registrations.end()) {
            return std::nullopt;
        }

        ReplyRegistration registration = std::move(it.value());
        registrations.erase(it);
        idsByReply.remove(registration.identity);
        if (disconnectConnections) {
            QObject::disconnect(registration.finishedConnection);
            QObject::disconnect(registration.destroyedConnection);
        }
        return registration;
    }

    [[nodiscard]] QList<ReplyRegistration> takeAllRegistrations()
    {
        QList<ReplyRegistration> result;
        result.reserve(registrations.size());
        for (auto it = registrations.begin(); it != registrations.end(); ++it) {
            result.append(std::move(it.value()));
        }
        registrations.clear();
        idsByReply.clear();
        for (const ReplyRegistration &registration : std::as_const(result)) {
            QObject::disconnect(registration.finishedConnection);
            QObject::disconnect(registration.destroyedConnection);
        }
        return result;
    }

    /// 同步撤销析构期关联；取消 reply 时屏蔽业务信号，且不依赖 DeferredDelete。
    void teardownForDestruction()
    {
        if (destructionTeardownComplete) {
            return;
        }
        destructionTeardownComplete = true;
        cancelled                   = true;

        if (autoTimeoutTimer) {
            autoTimeoutTimer->stop();
            QObject::disconnect(autoTimeoutTimer, nullptr, q_ptr, nullptr);
            autoTimeoutTimer = nullptr;
        }

        const QList<ReplyRegistration> registrationsToCancel = takeAllRegistrations();
        for (const ReplyRegistration &registration : registrationsToCancel) {
            QCNetworkReply *reply = registration.reply.data();
            if (!reply) {
                continue;
            }
            const QSignalBlocker blocker(reply);
            reply->cancel();
        }
    }

    QHash<quint64, ReplyRegistration> registrations;
    QHash<QCNetworkReply *, quint64> idsByReply;
    quint64 nextRegistrationId       = 1;
    QTimer *autoTimeoutTimer         = nullptr;
    bool cancelled                   = false;
    bool destructionTeardownComplete = false;

private:
    QCNetworkCancelToken *q_ptr = nullptr;
};

QCNetworkCancelToken::QCNetworkCancelToken(QObject *parent)
    : QObject(parent)
    , d_ptr(new QCNetworkCancelTokenPrivate(this))
{}

QCNetworkCancelToken::~QCNetworkCancelToken()
{
    Q_D(QCNetworkCancelToken);
    d->teardownForDestruction();
}

QCNetworkCancelToken::AttachResult QCNetworkCancelToken::attach(QCNetworkReply *reply)
{
    Q_D(QCNetworkCancelToken);

    if (!d->isOwnerThread()) {
        return AttachResult::WrongThread;
    }
    if (!reply) {
        return AttachResult::NullReply;
    }
    if (reply->thread() != thread()) {
        return AttachResult::ThreadAffinityMismatch;
    }
    if (d->cancelled) {
        return AttachResult::TokenCancelled;
    }
    if (d->idsByReply.contains(reply)) {
        return AttachResult::AlreadyAttached;
    }

    const quint64 registrationId = d->nextRegistrationId++;
    QCNetworkCancelTokenPrivate::ReplyRegistration registration;
    registration.id                 = registrationId;
    registration.identity           = reply;
    registration.reply              = reply;
    registration.finishedConnection = connect(
        reply,
        &QCNetworkReply::finished,
        this,
        [this, registrationId]() { onReplyFinished(registrationId); },
        Qt::DirectConnection);
    registration.destroyedConnection = connect(
        reply,
        &QObject::destroyed,
        this,
        [this, registrationId]() { onReplyDestroyed(registrationId); },
        Qt::DirectConnection);

    d->idsByReply.insert(reply, registrationId);
    d->registrations.insert(registrationId, std::move(registration));
    return AttachResult::Attached;
}

QList<QCNetworkCancelToken::AttachResult> QCNetworkCancelToken::attachMultiple(
    const QList<QCNetworkReply *> &replies)
{
    QList<AttachResult> results;
    results.reserve(replies.size());
    for (QCNetworkReply *reply : replies) {
        results.append(attach(reply));
    }
    return results;
}

QCNetworkCancelToken::CommandResult QCNetworkCancelToken::detach(QCNetworkReply *reply)
{
    Q_D(QCNetworkCancelToken);

    if (!d->isOwnerThread()) {
        return CommandResult::WrongThread;
    }
    if (!reply) {
        return CommandResult::InvalidArgument;
    }
    const auto id = d->idsByReply.constFind(reply);
    if (id == d->idsByReply.cend()) {
        return CommandResult::NoChange;
    }
    Q_UNUSED(d->takeRegistration(*id, true));
    return CommandResult::Applied;
}

QCNetworkCancelToken::CommandResult QCNetworkCancelToken::cancel()
{
    Q_D(QCNetworkCancelToken);

    if (!d->isOwnerThread()) {
        return CommandResult::WrongThread;
    }
    if (d->cancelled) {
        return CommandResult::NoChange;
    }

    d->cancelled = true;

    if (d->autoTimeoutTimer) {
        d->autoTimeoutTimer->stop();
        d->autoTimeoutTimer->deleteLater();
        d->autoTimeoutTimer = nullptr;
    }

    const QList<QCNetworkCancelTokenPrivate::ReplyRegistration> registrationsToCancel
        = d->takeAllRegistrations();
    for (const auto &registration : registrationsToCancel) {
        QCNetworkReply *reply = registration.reply.data();
        if (reply) {
            reply->cancel();
        }
    }

    Q_EMIT cancelled();
    return CommandResult::Applied;
}

QCNetworkCancelToken::CommandResult QCNetworkCancelToken::setAutoTimeout(int msecs)
{
    Q_D(QCNetworkCancelToken);

    if (!d->isOwnerThread()) {
        return CommandResult::WrongThread;
    }
    if (d->cancelled) {
        return CommandResult::NoChange;
    }
    if (msecs <= 0) {
        if (!d->autoTimeoutTimer) {
            return CommandResult::NoChange;
        }
        d->autoTimeoutTimer->stop();
        d->autoTimeoutTimer->deleteLater();
        d->autoTimeoutTimer = nullptr;
        return CommandResult::Applied;
    }

    if (!d->autoTimeoutTimer) {
        d->autoTimeoutTimer = new QTimer(this);
        connect(d->autoTimeoutTimer,
                &QTimer::timeout,
                this,
                &QCNetworkCancelToken::onAutoTimeoutTriggered);
    }

    d->autoTimeoutTimer->start(msecs);
    return CommandResult::Applied;
}

int QCNetworkCancelToken::attachedCount() const
{
    Q_D(const QCNetworkCancelToken);
    if (!d->isOwnerThread()) {
        Q_ASSERT_X(false, "QCNetworkCancelToken::attachedCount", "必须在 token owner thread 调用");
        return 0;
    }
    return d->registrations.size();
}

bool QCNetworkCancelToken::isCancelled() const
{
    Q_D(const QCNetworkCancelToken);
    if (!d->isOwnerThread()) {
        Q_ASSERT_X(false, "QCNetworkCancelToken::isCancelled", "必须在 token owner thread 调用");
        return false;
    }
    return d->cancelled;
}

QCNetworkCancelToken::CommandResult QCNetworkCancelToken::clear()
{
    Q_D(QCNetworkCancelToken);

    if (!d->isOwnerThread()) {
        return CommandResult::WrongThread;
    }
    if (d->registrations.isEmpty()) {
        return CommandResult::NoChange;
    }
    Q_UNUSED(d->takeAllRegistrations());
    return CommandResult::Applied;
}

void QCNetworkCancelToken::onReplyFinished(quint64 registrationId)
{
    Q_D(QCNetworkCancelToken);
    Q_ASSERT(d->isOwnerThread());
    const auto registration = d->takeRegistration(registrationId, true);
    if (!registration) {
        return;
    }
    QCNetworkReply *reply = registration->reply.data();
    if (!reply) {
        return;
    }
    Q_EMIT requestCompleted(reply);
}

void QCNetworkCancelToken::onReplyDestroyed(quint64 registrationId) noexcept
{
    Q_D(QCNetworkCancelToken);
    Q_ASSERT(d->isOwnerThread());
    Q_UNUSED(d->takeRegistration(registrationId, false));
}

void QCNetworkCancelToken::onAutoTimeoutTriggered()
{
    Q_D(QCNetworkCancelToken);
    if (!d->cancelled) {
        Q_UNUSED(cancel());
    }
}

} // namespace QCurl
