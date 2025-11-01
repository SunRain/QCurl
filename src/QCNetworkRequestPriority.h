// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#ifndef QCNETWORKREQUESTPRIORITY_H
#define QCNETWORKREQUESTPRIORITY_H

#include <QString>

namespace QCurl {

/**
 * @brief 网络请求的优先级枚举
 * 
 * 定义了请求的执行优先级，用于请求调度器按优先级排序和执行请求。
 * 
 */
enum class QCNetworkRequestPriority {
    /**
     * @brief 极低优先级
     * 
     * 用于后台任务、预加载、缓存刷新等非关键操作。
     * 这些请求会在所有其他请求完成后执行。
     */
    VeryLow = 0,
    
    /**
     * @brief 低优先级
     * 
     * 用于非关键请求，如统计数据上报、日志上传等。
     * 对用户体验影响较小的操作。
     */
    Low = 1,
    
    /**
     * @brief 正常优先级（默认）
     * 
     * 大多数请求的默认优先级。
     * 用于常规业务逻辑、数据获取等操作。
     */
    Normal = 2,
    
    /**
     * @brief 高优先级
     * 
     * 用于与用户交互直接相关的请求。
     * 如用户点击按钮触发的数据加载、表单提交等。
     */
    High = 3,
    
    /**
     * @brief 极高优先级
     * 
     * 用于关键业务请求，如支付确认、订单提交等。
     * 这些请求应尽快执行。
     */
    VeryHigh = 4,
    
    /**
     * @brief 紧急优先级
     * 
     * 最高优先级，跳过队列立即执行。
     * 用于紧急通知、实时数据更新、安全相关请求等。
     */
    Critical = 5
};

/**
 * @brief 将优先级枚举转换为字符串
 * 
 * @param priority 优先级枚举值
 * @return 优先级的字符串表示
 * 
 * @code
 * QString str = toString(QCNetworkRequestPriority::High);
 * // str == "High"
 * @endcode
 */
inline QString toString(QCNetworkRequestPriority priority) {
    switch (priority) {
    case QCNetworkRequestPriority::VeryLow:  return QStringLiteral("VeryLow");
    case QCNetworkRequestPriority::Low:      return QStringLiteral("Low");
    case QCNetworkRequestPriority::Normal:   return QStringLiteral("Normal");
    case QCNetworkRequestPriority::High:     return QStringLiteral("High");
    case QCNetworkRequestPriority::VeryHigh: return QStringLiteral("VeryHigh");
    case QCNetworkRequestPriority::Critical: return QStringLiteral("Critical");
    default:                                  return QStringLiteral("Unknown");
    }
}

/**
 * @brief 从字符串解析优先级枚举
 * 
 * @param str 优先级的字符串表示（不区分大小写）
 * @param ok 可选的输出参数，指示解析是否成功
 * @return 解析的优先级枚举值，失败时返回 Normal
 * 
 * @code
 * bool ok;
 * auto priority = fromString("High", &ok);
 * if (ok) {
 *     // 解析成功，priority == QCNetworkRequestPriority::High
 * }
 * @endcode
 */
inline QCNetworkRequestPriority fromString(const QString &str, bool *ok = nullptr) {
    QString lowerStr = str.toLower();
    
    if (lowerStr == QStringLiteral("verylow")) {
        if (ok) *ok = true;
        return QCNetworkRequestPriority::VeryLow;
    } else if (lowerStr == QStringLiteral("low")) {
        if (ok) *ok = true;
        return QCNetworkRequestPriority::Low;
    } else if (lowerStr == QStringLiteral("normal")) {
        if (ok) *ok = true;
        return QCNetworkRequestPriority::Normal;
    } else if (lowerStr == QStringLiteral("high")) {
        if (ok) *ok = true;
        return QCNetworkRequestPriority::High;
    } else if (lowerStr == QStringLiteral("veryhigh")) {
        if (ok) *ok = true;
        return QCNetworkRequestPriority::VeryHigh;
    } else if (lowerStr == QStringLiteral("critical")) {
        if (ok) *ok = true;
        return QCNetworkRequestPriority::Critical;
    }
    
    if (ok) *ok = false;
    return QCNetworkRequestPriority::Normal;  // 默认返回 Normal
}

} // namespace QCurl

#endif // QCNETWORKREQUESTPRIORITY_H
