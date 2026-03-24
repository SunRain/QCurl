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

} // namespace QCurl::TestEnv

#endif // QCURL_TEST_HTTPBIN_ENV_H
