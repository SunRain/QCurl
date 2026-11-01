#ifndef QCNETWORK_TRACE_MODE_H
#define QCNETWORK_TRACE_MODE_H

#include <QDebug>

#include <cstdio>
#include <curl/curl.h>

/// 每个测试进程仅在首个 transfer 前设置一次，不能在 QtTest 数据行中切换全局配置。
inline bool configureTraceMode()
{
    const QByteArray mode = qgetenv("QCURL_TEST_LIB_IDS");
    if (!mode.isEmpty() && mode != "on" && mode != "off") {
        qCritical() << "无效的 lib-ids 测试模式";
        return false;
    }
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        qCritical() << "libcurl 全局初始化失败";
        return false;
    }
    const auto *version = curl_version_info(CURLVERSION_NOW);
    // strict 门禁默认屏蔽 Qt info；启动证据直接写入进程输出，不依赖日志级别。
    std::fprintf(stderr,
                 "libcurl=%s threadsafe=%s lib-ids=%s\n",
                 version->version,
                 version->features & CURL_VERSION_THREADSAFE ? "true" : "false",
                 mode == "on" ? "on" : "off");
    if (curl_global_trace(mode == "on" ? "lib-ids" : "-lib-ids") != CURLE_OK) {
        qCritical() << "libcurl trace 配置失败";
        curl_global_cleanup();
        return false;
    }
    return true;
}

#endif // QCNETWORK_TRACE_MODE_H
