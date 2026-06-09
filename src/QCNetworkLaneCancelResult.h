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

/** manager-level lane 取消操作的结构化结果。 */
class QCURL_EXPORT QCNetworkLaneCancelResult
{
public:
    enum class Status {
        Success,
        InvalidLane,
        UnregisteredLane,
        NonOwnerThread,
        SchedulerDisabled,
    };

    QCNetworkLaneCancelResult();
    QCNetworkLaneCancelResult(const QCNetworkLaneCancelResult &other);
    QCNetworkLaneCancelResult(QCNetworkLaneCancelResult &&other) noexcept;
    ~QCNetworkLaneCancelResult();

    QCNetworkLaneCancelResult &operator=(const QCNetworkLaneCancelResult &other);
    QCNetworkLaneCancelResult &operator=(QCNetworkLaneCancelResult &&other) noexcept;

    [[nodiscard]] static QCNetworkLaneCancelResult success(int cancelledRequests);
    [[nodiscard]] static QCNetworkLaneCancelResult failure(Status status, const QString &error);

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
