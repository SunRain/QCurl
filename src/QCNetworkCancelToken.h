// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

/**
 * @file
 * @brief 声明请求取消令牌。
 */

#ifndef QCNETWORKCANCELTOKEN_H
#define QCNETWORKCANCELTOKEN_H

#include "QCGlobal.h"

#include <QList>
#include <QObject>
#include <QScopedPointer>

// moc 需要完整 reply 类型来生成信号参数元类型代码。
Q_MOC_INCLUDE("QCNetworkReply.h")

namespace QCurl {

class QCNetworkReply;
class QCNetworkCancelTokenPrivate;

/**
 * @brief 网络请求取消令牌
 *
 * 用于管理和取消多个 reply-level 网络请求，支持以下功能：
 * - 一键取消多个请求
 * - 自动超时取消
 * - 批量请求管理
 *
 *
 * @example
 * @code
 * auto token = new QCNetworkCancelToken();
 *
 * // 附加 reply 到令牌
 * auto *reply1 = manager->get(request1);
 * auto *reply2 = manager->get(request2);
 * Q_ASSERT(token->attach(reply1) == QCNetworkCancelToken::AttachResult::Attached);
 * Q_ASSERT(token->attach(reply2) == QCNetworkCancelToken::AttachResult::Attached);
 *
 * // 一键取消所有请求
 * Q_ASSERT(token->cancel() == QCNetworkCancelToken::CommandResult::Applied);
 *
 * // 或设置自动超时取消（30秒后自动取消）
 * Q_ASSERT(token->setAutoTimeout(30000) == QCNetworkCancelToken::CommandResult::Applied);
 * @endcode
 *
 * @note 错误生命周期：`AttachResult` 与 `CommandResult` 是每次同步调用的独立纯值结果；
 * 枚举是权威分类，成功不会保留历史错误，拒绝不会改写既有 registration。
 * @note QObject 借用合同：token 不拥有 reply；空指针会被拒绝，成功附着要求相同 affinity。
 * reply 完成、析构、detach/clear/cancel 或 token 析构均使借用失效，命令只在 owner thread 调用。
 */
class QCURL_EXPORT QCNetworkCancelToken : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 表示单个 reply 附着请求的同步结果。
     *
     * 结果在 `attach()` 返回后不再变化；除 `Attached` 外均保证 token 状态不变。
     */
    enum class AttachResult {
        Attached,
        AlreadyAttached,
        NullReply,
        WrongThread,
        ThreadAffinityMismatch,
        TokenCancelled,
    };
    Q_ENUM(AttachResult)

    /**
     * @brief 表示取消令牌同步命令的接收结果。
     */
    enum class CommandResult {
        Applied,
        NoChange,
        WrongThread,
        InvalidArgument,
    };
    Q_ENUM(CommandResult)

    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit QCNetworkCancelToken(QObject *parent = nullptr);

    /**
     * @brief 析构函数
     *
     * 析构时自动取消所有附加的 reply。
     */
    ~QCNetworkCancelToken() override;

    /**
     * @brief 将 reply 附加到此令牌。
     * @param reply 非 owning reply 指针；不可为空，且必须与 token 具有相同 thread affinity。
     * @return 同步附着结果；失败时不修改已有 registration。
     * @note 本函数只能在 token owner thread 调用。成功后 token 观察 reply，reply 析构、完成、
     * `detach()`、`clear()`、`cancel()` 或 token 析构均会使该借用失效。
     */
    [[nodiscard]] AttachResult attach(QCNetworkReply *reply);

    /**
     * @brief 按输入顺序附着多个 reply。
     * @param replies 非 owning reply 指针列表；每个元素独立校验。
     * @return 与输入一一对应的附着结果列表。
     * @note 本函数只能在 token owner thread 调用。
     */
    [[nodiscard]] QList<AttachResult> attachMultiple(const QList<QCNetworkReply *> &replies);

    /**
     * @brief 从令牌中移除 reply。
     * @param reply 非 owning reply 指针；仅用来匹配现有 registration。
     * @return 成功移除时返回 `Applied`；未附着时返回 `NoChange`。
     * @note 本函数只能在 token owner thread 调用。
     */
    [[nodiscard]] CommandResult detach(QCNetworkReply *reply);

    /**
     * @brief 取消令牌中的所有 reply。
     * @return 首次取消返回 `Applied`，重复取消返回 `NoChange`。
     * @note 本函数只能在 token owner thread 调用。
     */
    [[nodiscard]] CommandResult cancel();

    /**
     * @brief 设置自动超时取消。
     * @param msecs 超时时间（毫秒）；小于等于 0 表示禁用自动超时。
     * @return 定时器状态已应用时返回 `Applied`，无需变更时返回 `NoChange`。
     * @note 本函数只能在 token owner thread 调用。
     */
    [[nodiscard]] CommandResult setAutoTimeout(int msecs);

    /**
     * @brief 获取当前有效 registration 数量。
     * @note 本函数只能在 token owner thread 调用。
     */
    [[nodiscard]] int attachedCount() const;

    /**
     * @brief 检查令牌是否已被取消。
     * @note 本函数只能在 token owner thread 调用。
     */
    [[nodiscard]] bool isCancelled() const;

    /**
     * @brief 清空所有附加的请求，但不取消 reply。
     * @return 清除了 registration 时返回 `Applied`，原本为空时返回 `NoChange`。
     * @note 本函数只能在 token owner thread 调用。
     */
    [[nodiscard]] CommandResult clear();

Q_SIGNALS:
    /**
     * @brief 当令牌被取消时发射此信号
     */
    void cancelled();

    /**
     * @brief 当某个请求完成时发射此信号
     * @param reply 已完成的响应对象
     */
    void requestCompleted(QCNetworkReply *reply);

private Q_SLOTS:
    void onAutoTimeoutTriggered();

private:
    Q_DISABLE_COPY_MOVE(QCNetworkCancelToken)

    void onReplyFinished(quint64 registrationId);
    void onReplyDestroyed(quint64 registrationId) noexcept;

    Q_DECLARE_PRIVATE(QCNetworkCancelToken)
    QScopedPointer<QCNetworkCancelTokenPrivate> d_ptr;
};

} // namespace QCurl

#endif // QCNETWORKCANCELTOKEN_H
