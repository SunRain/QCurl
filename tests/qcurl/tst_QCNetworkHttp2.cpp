/**
 * @file tst_QCNetworkHttp2.cpp
 * @brief 覆盖 HTTP/2 协商、复用、降级和本地 TLS 场景。
 *
 * 默认使用 `tests/qcurl/http2-test-server.js` 启动本地可控 server。
 * 也可通过 `QCURL_HTTP2_TEST_BASE_URL` 和
 * `QCURL_HTTP2_TEST_HTTP1_BASE_URL` 覆盖 HTTP/2 与 HTTP/1.1 入口。
 * `QCURL_HTTP2_DISABLE_DOWNGRADE_TEST=1` 仅用于 gate/planner 侧显式排除降级用例，
 * 不应依赖 suite 内部跳过。
 */

#include "qcnetwork_http2_test.h"
#include "qcnetwork_trace_mode.h"

#include <QCoreApplication>
#include <QtTest>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (!configureTraceMode()) {
        return 1;
    }
    TestQCNetworkHttp2 test;
    const int result = QTest::qExec(&test, argc, argv);
    curl_global_cleanup();
    return result;
}
