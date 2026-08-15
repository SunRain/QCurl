#include "QCCurlMultiManager.h"

#include <QString>

namespace QCurl {

QCCurlMultiManager::ShareContext *QCCurlMultiManager::prepareCookieContextLocked(
    const QCNetworkAccessManager *manager,
    const ShareConfig &desired,
    Internal::CookieStoreResult *failure)
{
    ShareContext *context = getOrCreateShareContextLocked(manager);
    if (!context) {
        if (failure) {
            failure->status     = Internal::CookieStoreStatus::RejectedBeforeMutation;
            failure->policyCode = QStringLiteral("cookie.share_context_unavailable");
            failure->message    = QStringLiteral("share context 不可用");
        }
        return nullptr;
    }

    if (context->cookieStorePoisoned) {
        if (failure) {
            failure->status     = Internal::CookieStoreStatus::StorePoisoned;
            failure->policyCode = QStringLiteral("cookie.store_poisoned");
            failure->message    = QStringLiteral("cookie store 已进入不可恢复状态");
        }
        return nullptr;
    }

    if (!context->share || (context->applied != desired)) {
        if (context->activeUsers != 0) {
            if (failure) {
                failure->status     = Internal::CookieStoreStatus::RejectedBeforeMutation;
                failure->policyCode = QStringLiteral("cookie.share_busy");
                failure->message    = QStringLiteral("share handle 正在使用中，无法切换配置");
            }
            return nullptr;
        }

        QString error;
        if (!applyShareConfigIfIdleLocked(context, desired, &error)) {
            if (failure) {
                *failure = context->lastInitResult;
                if (failure->message.isEmpty()) {
                    failure->status     = Internal::CookieStoreStatus::RejectedBeforeMutation;
                    failure->policyCode = QStringLiteral("cookie.share_init_failed");
                    failure->message    = error;
                }
            }
            return nullptr;
        }
    }

    if (!context->share || !context->applied.cookies) {
        if (failure) {
            failure->status     = Internal::CookieStoreStatus::RejectedBeforeMutation;
            failure->policyCode = QStringLiteral("cookie.share_disabled");
            failure->message    = QStringLiteral("share cookie store 未启用");
        }
        return nullptr;
    }

    return context;
}

} // namespace QCurl
