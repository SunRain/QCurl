#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace QCurl::TestEnv {

inline QString httpbinBaseUrl()
{
    QString url = QString::fromUtf8(qgetenv("QCURL_HTTPBIN_URL")).trimmed();
    while (url.endsWith('/')) {
        url.chop(1);
    }
    return url;
}

inline QString httpbinMissingReason()
{
    return QStringLiteral("未配置 httpbin：请先启动 tests/httpbin/start_httpbin.sh 并 source "
                          "输出的 env 文件，或设置环境变量 QCURL_HTTPBIN_URL。");
}

} // namespace QCurl::TestEnv
