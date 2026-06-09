// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

/**
 * @file
 * @brief 声明请求调度 lane 的 typed key。
 */

#ifndef QCNETWORKLANEKEY_H
#define QCNETWORKLANEKEY_H

#include "QCGlobal.h"

#include <QDebug>
#include <QSharedDataPointer>
#include <QString>

namespace QCurl {

class QCNetworkLaneKeyData;

/**
 * @brief 请求调度 lane 的轻量 typed key。
 *
 * 空名称表示 default lane；自定义 lane 必须先注册到 QCNetworkSchedulerPolicy 后才能调度。
 */
class QCURL_EXPORT QCNetworkLaneKey
{
public:
    QCNetworkLaneKey();
    QCNetworkLaneKey(const QCNetworkLaneKey &other);
    QCNetworkLaneKey(QCNetworkLaneKey &&other) noexcept;
    ~QCNetworkLaneKey();

    QCNetworkLaneKey &operator=(const QCNetworkLaneKey &other);
    QCNetworkLaneKey &operator=(QCNetworkLaneKey &&other) noexcept;

    [[nodiscard]] QString name() const;
    [[nodiscard]] bool isValid() const;
    [[nodiscard]] bool isDefault() const;

    [[nodiscard]] static QCNetworkLaneKey defaultLane();
    [[nodiscard]] static QCNetworkLaneKey control();
    [[nodiscard]] static QCNetworkLaneKey transfer();
    [[nodiscard]] static QCNetworkLaneKey background();
    /**
     * @brief 从自定义 lane 名称解析 typed key。
     *
     * 失败时返回 false，`out` 保持不变，`error` 非空时写入可诊断原因。
     * 空名称不是自定义 lane；默认 lane 请使用 `defaultLane()`。
     */
    [[nodiscard]] static bool fromName(const QString &name,
                                       QCNetworkLaneKey *out,
                                       QString *error = nullptr);

    friend bool operator==(const QCNetworkLaneKey &lhs, const QCNetworkLaneKey &rhs)
    {
        return lhs.name() == rhs.name() && lhs.isValid() == rhs.isValid();
    }

    friend bool operator!=(const QCNetworkLaneKey &lhs, const QCNetworkLaneKey &rhs)
    {
        return !(lhs == rhs);
    }

private:
    explicit QCNetworkLaneKey(QString name, bool valid);

    QSharedDataPointer<QCNetworkLaneKeyData> d;
};

QCURL_EXPORT QDebug operator<<(QDebug dbg, const QCNetworkLaneKey &lane);

} // namespace QCurl

Q_DECLARE_METATYPE(QCurl::QCNetworkLaneKey)

#endif // QCNETWORKLANEKEY_H
