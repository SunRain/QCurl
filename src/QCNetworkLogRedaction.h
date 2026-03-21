// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#ifndef QCNETWORKLOGREDACTION_H
#define QCNETWORKLOGREDACTION_H

#include "QCGlobal.h"

#include <QByteArray>
#include <QString>
#include <QUrl>

/**
 * @brief 网络日志脱敏工具
 *
 * 用于在日志输出前脱敏 URL 查询参数和常见敏感请求头。
 */
namespace QCurl {
namespace QCNetworkLogRedaction {

/**
 * @brief 判断查询参数 key 是否敏感
 * @param keyLower 已转换为小写的 key
 * @return true 表示需要脱敏
 */
[[nodiscard]] QCURL_EXPORT bool isSensitiveQueryKey(const QString &keyLower);

/**
 * @brief 判断 HTTP 头 key 是否敏感
 * @param keyLower 已转换为小写的 key
 * @return true 表示需要脱敏
 */
[[nodiscard]] QCURL_EXPORT bool isSensitiveHeaderKey(const QByteArray &keyLower);

/**
 * @brief 脱敏 URL 查询参数
 *
 * @param line 可能包含查询参数的任意文本
 * @return 脱敏后的文本；仅保证处理当前实现识别的敏感 key 集合
 */
[[nodiscard]] QCURL_EXPORT QString redactSensitiveQueryParams(const QString &line);

/**
 * @brief 脱敏 libcurl trace 行
 *
 * 支持对 trace 中的 URL 查询参数与 HTTP 头进行脱敏，并去除末尾 CR/LF。
 *
 * @param line trace 行（原始字节）
 * @return 脱敏后的文本
 */
[[nodiscard]] QCURL_EXPORT QString redactSensitiveTraceLine(const QByteArray &line);

/**
 * @brief 脱敏 URL
 * @param url 原始 URL
 * @return 脱敏后的 URL 字符串
 */
[[nodiscard]] QCURL_EXPORT QString redactUrl(const QUrl &url);

} // namespace QCNetworkLogRedaction
} // namespace QCurl

#endif // QCNETWORKLOGREDACTION_H
