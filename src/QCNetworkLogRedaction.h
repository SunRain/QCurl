// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#ifndef QCNETWORKLOGREDACTION_H
#define QCNETWORKLOGREDACTION_H

#include <QByteArray>
#include <QString>
#include <QUrl>

/**
 * @brief 网络日志脱敏工具
 *
 * 用于将 URL 查询参数与 HTTP 头中的敏感信息替换为占位符，避免写入日志。
 */
namespace QCurl::QCNetworkLogRedaction {

/**
 * @brief 判断查询参数 key 是否敏感
 * @param keyLower 已转换为小写的 key
 * @return true 表示需要脱敏
 */
[[nodiscard]] bool isSensitiveQueryKey(const QString &keyLower);

/**
 * @brief 判断 HTTP 头 key 是否敏感
 * @param keyLower 已转换为小写的 key
 * @return true 表示需要脱敏
 */
[[nodiscard]] bool isSensitiveHeaderKey(const QByteArray &keyLower);

/**
 * @brief 脱敏 URL 查询参数
 *
 * 将 `token/api_key/password` 等敏感 key 的 value 替换为 `[REDACTED]`。
 *
 * @param line 可能包含查询参数的任意文本
 * @return 脱敏后的文本
 */
[[nodiscard]] QString redactSensitiveQueryParams(const QString &line);

/**
 * @brief 脱敏 libcurl trace 行
 *
 * 支持对 trace 中的 URL 查询参数与 HTTP 头进行脱敏（并去除末尾 CR/LF）。
 *
 * @param line trace 行（原始字节）
 * @return 脱敏后的文本
 */
[[nodiscard]] QString redactSensitiveTraceLine(const QByteArray &line);

/**
 * @brief 脱敏 URL
 * @param url 原始 URL
 * @return 脱敏后的 URL 字符串
 */
[[nodiscard]] QString redactUrl(const QUrl &url);

} // namespace QCurl::QCNetworkLogRedaction

#endif // QCNETWORKLOGREDACTION_H
