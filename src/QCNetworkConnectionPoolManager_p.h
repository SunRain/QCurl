// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

/**
 * @file
 * @brief 声明连接池管理器的内部 companion API。
 */

#ifndef QCNETWORKCONNECTIONPOOLMANAGER_P_H
#define QCNETWORKCONNECTIONPOOLMANAGER_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the QCurl API. It exists purely as an
// implementation detail. This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include <QString>

namespace QCurl::Internal {

/**
 * @brief Reply 与连接池管理器之间的内部桥接接口
 *
 * 仅供库内请求路径调用，用于把 curl handle 配置和统计逻辑收口到 private header。
 */
class QCNetworkConnectionPoolManagerInternal
{
public:
    /// 为内部 curl handle 应用连接池配置。
    static void configureCurlHandle(void *handle, const QString &host);

    /// 记录一次请求完成及其连接复用结果。
    static void recordRequestCompleted(void *handle, bool wasReused);
};

} // namespace QCurl::Internal

#endif // QCNETWORKCONNECTIONPOOLMANAGER_P_H
