/**
 * @file
 * @brief 声明 share/Cookie 操作的结构化结果。
 */
#ifndef QCCURLSHARECOOKIERESULT_P_H
#define QCCURLSHARECOOKIERESULT_P_H

#include "private/QCCookieStoreResult_p.h"

#include <QStringList>

namespace QCurl::Internal {

/// Callable 可包含延后配置或安全的初始化失败；NonCallable 禁止继续原生清理。
enum class ShareCallState {
    Callable,
    Rejected,
    NonCallable,
};

struct ShareCallResult
{
    ShareCallState state{ShareCallState::Callable};
    QString operation;
    CURLcode curlCode    = CURLE_OK;
    CURLSHcode shareCode = CURLSHE_OK;
    QString message;
    QStringList warnings;
};

/// 事务成功不代表临时 easy 已安全退出；发布方必须先消费 lifecycle。
struct CookieCommandResult
{
    CookieStoreResult store;
    ShareCallResult lifecycle;
};

} // namespace QCurl::Internal

#endif // QCCURLSHARECOOKIERESULT_P_H
