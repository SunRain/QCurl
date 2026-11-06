#ifndef QCCURLSHARECOOKIESTORETESTACCESS_P_H
#define QCCURLSHARECOOKIESTORETESTACCESS_P_H

#include "private/QCCurlShareCookieStore_p.h"

#ifdef QCURL_ENABLE_TEST_HOOKS

namespace QCurl::Internal {

/// 测试只观察 ownership 事实；不向调用方暴露 context 或 CURLSH。
struct ShareCookieSnapshot
{
    int bindings           = 0;
    int contexts           = 0;
    int handles            = 0;
    int users              = 0;
    int operations         = 0;
    int retainedOperations = 0;
    QCNetworkAccessManager::ShareHandleConfig applied;
    bool pending = false;
};

class Q_DECL_HIDDEN QCCurlShareCookieStoreTestAccess final
{
public:
    [[nodiscard]] static ShareCookieSnapshot snapshot(
        const QCCurlShareCookieStore &store, const QCNetworkAccessManager *manager = nullptr);
    [[nodiscard]] static bool hasBinding(const QCCurlShareCookieStore &store, CURL *easy);
    [[nodiscard]] static ShareCallResult prepareDetach(QCCurlShareCookieStore &store, CURL *easy);
    [[nodiscard]] static ShareCallResult prepareRollback(QCCurlShareCookieStore &store, CURL *easy);
    static void observe(QCCurlShareCookieStore &store,
                        void (*observer)(void *, const char *),
                        void *context);
};

} // namespace QCurl::Internal

#endif // QCURL_ENABLE_TEST_HOOKS
#endif // QCCURLSHARECOOKIESTORETESTACCESS_P_H
