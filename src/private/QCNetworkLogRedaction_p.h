// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

/**
 * @file
 * @brief 声明日志脱敏辅助接口（内部）。
 */

#ifndef QCNETWORKLOGREDACTION_P_H
#define QCNETWORKLOGREDACTION_P_H

#include "QCGlobal.h"
#include "QCNetworkDiagnosticQueryPolicy.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QUrl>

/**
 * @brief 网络日志脱敏工具
 *
 * 用于在日志输出前脱敏 URL 查询参数和常见敏感请求头。
 *
 * @internal
 */
namespace QCurl {
namespace QCNetworkLogRedaction {

/**
 * @brief 判断 HTTP 头 key 是否敏感
 * @param keyLower 已转换为小写的 key
 * @return true 表示需要脱敏
 */
[[nodiscard]] bool isSensitiveHeaderKey(const QByteArray &keyLower);

struct RedactedTraceLine
{
    QString text;
    bool protectContinuation = false;
    bool credentialRecord    = false;
};

/**
 * @brief 隐藏文本中的完整 URL query
 *
 * @param line 可能包含 URL、request-target 或重定向位置的文本
 * @return 脱敏后的文本；URL query 统一隐藏，URL userinfo/fragment 移除，不改普通文本中的 #。
 * URL 记录无法完整判界或解析时仅输出固定安全标记；多行输入逐行处理并保留换行形式。
 */
[[nodiscard]] QString redactSensitiveQueryParams(const QString &line);

/**
 * @brief 脱敏 libcurl trace 行
 *
 * 一次解析同时处理 URL、HTTP 头和 Cookie/认证 TEXT，并决定后续缩进续行是否需保护。
 * 疑似凭据或敏感头解析失败时只返回固定安全标记，不能按普通文本回退。
 * 凭据记录另行标记：用户名可含换行和引号，文本行尾不能独自证明其原始记录已结束。
 *
 * @param line trace 行（原始字节）
 * @return 脱敏文本及续行保护分类；不保存借用的原始字节。
 */
[[nodiscard]] RedactedTraceLine redactSensitiveTraceLine(QByteArrayView line);

/**
 * @brief 脱敏 URL
 * @param url 原始 URL
 * @param policy 请求级 query 策略；默认隐藏，Full 仅用于调用方初始 URL。
 * @return 移除 userinfo/fragment 的编码 URL；Redacted 的 query 显示为 ?[REDACTED]。
 */
[[nodiscard]] QString redactUrl(
    const QUrl &url,
    QCNetworkDiagnosticQueryPolicy policy = QCNetworkDiagnosticQueryPolicy::Redacted);

} // namespace QCNetworkLogRedaction
} // namespace QCurl

#endif // QCNETWORKLOGREDACTION_P_H
