// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

/**
 * @file
 * @brief 声明 scheduler lane 取消操作的结构化结果。
 */

#ifndef QCNETWORKLANECANCELRESULT_H
#define QCNETWORKLANECANCELRESULT_H

#include "QCGlobal.h"

#include <QMetaType>
#include <QSharedDataPointer>
#include <QString>

namespace QCurl {

class QCNetworkLaneCancelResultData;

/**
 * @brief manager-level lane 取消操作的结构化结果。
 *
 * @note 错误生命周期：结果构造后不再变化。成功时 `error()` 为空且取消数量非负；失败时
 * `status()` 是权威分类、取消数量为零，`error()` 只提供诊断文本。
 */
class QCURL_EXPORT QCNetworkLaneCancelResult
{
public:
    enum class Status {
        Success,
        InvalidLane,
        UnregisteredLane,
        NonOwnerThread,
        SchedulerDisabled,
        InvalidScope,
    };

    enum class FailureReason {
        InvalidLane,
        UnregisteredLane,
        NonOwnerThread,
        SchedulerDisabled,
        InvalidScope,
    };

    QCNetworkLaneCancelResult();
    QCNetworkLaneCancelResult(const QCNetworkLaneCancelResult &other);
    QCNetworkLaneCancelResult(QCNetworkLaneCancelResult &&other) noexcept;
    ~QCNetworkLaneCancelResult();

    QCNetworkLaneCancelResult &operator=(const QCNetworkLaneCancelResult &other);
    QCNetworkLaneCancelResult &operator=(QCNetworkLaneCancelResult &&other) noexcept;

    /**
     * @brief 构造成功结果。
     *
     * `cancelledRequests` 必须非负；debug 构建会断言此前置条件。
     */
    [[nodiscard]] static QCNetworkLaneCancelResult success(int cancelledRequests);
    /**
     * @brief 构造失败结果。
     *
     * `FailureReason` 不包含 Success，避免 public factory 构造矛盾状态。
     * 空 `error` 会被替换为 reason 对应的默认诊断。
     */
    [[nodiscard]] static QCNetworkLaneCancelResult failure(FailureReason reason,
                                                           const QString &error);

    [[nodiscard]] Status status() const noexcept;
    [[nodiscard]] bool isSuccess() const noexcept;
    [[nodiscard]] int cancelledRequests() const noexcept;
    [[nodiscard]] QString error() const;

private:
    QSharedDataPointer<QCNetworkLaneCancelResultData> d;
};

} // namespace QCurl

Q_DECLARE_METATYPE(QCurl::QCNetworkLaneCancelResult)

#endif // QCNETWORKLANECANCELRESULT_H
