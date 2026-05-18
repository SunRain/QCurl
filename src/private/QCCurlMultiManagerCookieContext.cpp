#include "QCCurlMultiManager.h"

#include <QString>

namespace QCurl {

QCCurlMultiManager::ShareContext *QCCurlMultiManager::prepareCookieContextLocked(
    const QCNetworkAccessManager *manager, const ShareConfig &desired, QString *error)
{
    ShareContext *context = getOrCreateShareContextLocked(manager);
    if (!context) {
        if (error) {
            *error = QStringLiteral("share context 不可用");
        }
        return nullptr;
    }

    if (!context->share || (context->applied != desired)) {
        if (context->activeUsers != 0) {
            if (error) {
                *error = QStringLiteral("share handle 正在使用中，无法初始化/切换配置");
            }
            return nullptr;
        }

        QString err;
        if (!applyShareConfigIfIdleLocked(context, desired, &err)) {
            if (error) {
                *error = err;
            }
            return nullptr;
        }
    }

    if (!context->share || !context->applied.cookies) {
        if (error) {
            *error = QStringLiteral("share cookie store 未启用");
        }
        return nullptr;
    }

    return context;
}

} // namespace QCurl
