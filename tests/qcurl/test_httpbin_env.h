/**
 * @file test_httpbin_env.h
 * @brief 读取 `tests/qcurl` 依赖的 httpbin 环境变量约定。
 */

#ifndef QCURL_TEST_HTTPBIN_ENV_H
#define QCURL_TEST_HTTPBIN_ENV_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace QCurl::TestEnv {

/// @brief 返回去除尾部 `/` 后的 httpbin base URL。
/// @return `QCURL_HTTPBIN_URL` 的规范化值；未配置时为空。
inline QString httpbinBaseUrl()
{
    QString url = QString::fromUtf8(qgetenv("QCURL_HTTPBIN_URL")).trimmed();
    while (url.endsWith('/')) {
        url.chop(1);
    }
    return url;
}

/// @brief 返回缺失 httpbin 配置时的统一提示。
/// @return 指向 `start_httpbin.sh` 和 `QCURL_HTTPBIN_URL` 的说明文本。
inline QString httpbinMissingReason()
{
    return QStringLiteral("未配置 httpbin：请先启动 tests/qcurl/httpbin/start_httpbin.sh 并 source "
                          "输出的 env 文件，或设置环境变量 QCURL_HTTPBIN_URL。");
}

/// @brief 返回 httpbin 不可达时的统一提示。
/// @param baseUrl 规范化后的 httpbin base URL。
/// @param detail 额外错误细节，可为空。
/// @return 统一的失败说明文本。
inline QString httpbinUnavailableReason(const QString &baseUrl, const QString &detail = {})
{
    if (baseUrl.trimmed().isEmpty()) {
        return httpbinMissingReason();
    }
    if (detail.trimmed().isEmpty()) {
        return QStringLiteral("httpbin 服务不可用：%1").arg(baseUrl);
    }
    return QStringLiteral("httpbin 服务不可用：%1 (%2)").arg(baseUrl, detail);
}

} // namespace QCurl::TestEnv

#endif // QCURL_TEST_HTTPBIN_ENV_H
