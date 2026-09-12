// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

/**
 * @file
 * @brief 声明跨模块共享的网络基础类型。
 */

#ifndef QCNETWORKTYPES_H
#define QCNETWORKTYPES_H

#include <QByteArray>
#include <QPair>

namespace QCurl {

// ==================
// 类型定义
// ==================

/**
 * @brief HTTP 原始头的键值对（字段名, 字段值）。
 *
 * 这是全库唯一的定义点。`QCNetworkReply`、`QCNetworkCache`、
 * `QCNetworkMockHandler` 等所有需要表达有序原始响应头的接口共用此类型，
 * 保留重复字段与字段名大小写，符合 RFC 9110 的 field order 语义。
 */
using RawHeaderPair = QPair<QByteArray, QByteArray>;

} // namespace QCurl

#endif // QCNETWORKTYPES_H
