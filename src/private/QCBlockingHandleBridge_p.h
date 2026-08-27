/**
 * @file
 * @brief 声明 Blocking Extras 复用 Core 句柄与协议策略的最小私有桥接。
 */

#ifndef QCBLOCKINGHANDLEBRIDGE_P_H
#define QCBLOCKINGHANDLEBRIDGE_P_H

#include "QCGlobal.h"

#include <QScopedPointer>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>

typedef void CURL;

namespace QCurl {

class QCCurlHandleManager;

struct QCBlockingRuntimeAvailability
{
    bool supported = false;
    QString reason;
};

/** @brief 持有一个由 Core 管理的 easy handle，且不暴露 QCCurlHandleManager。 */
class QCBlockingHandleBridge final
{
public:
    QCBlockingHandleBridge();
    ~QCBlockingHandleBridge();

    Q_DISABLE_COPY_MOVE(QCBlockingHandleBridge)

    [[nodiscard]] CURL *handle() const noexcept;
    [[nodiscard]] QString initializationError() const;

private:
    QScopedPointer<QCCurlHandleManager> m_manager;
};

[[nodiscard]] QCBlockingRuntimeAvailability blockingRuntimeAvailability();
[[nodiscard]] bool validateBlockingUrl(const QUrl &url, QString *error);
[[nodiscard]] bool resolveBlockingInitialProtocols(
    const std::optional<QStringList> &requested, QStringList *effective, QString *error);
[[nodiscard]] bool resolveBlockingRedirectProtocols(
    const std::optional<QStringList> &requested, QStringList *effective, QString *error);

} // namespace QCurl

#endif // QCBLOCKINGHANDLEBRIDGE_P_H
