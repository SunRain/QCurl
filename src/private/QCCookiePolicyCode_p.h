/**
 * @file
 * @brief 定义现有 Cookie 私有诊断文本码，不替代 QCCookieAsyncError 分类。
 */

#ifndef QCCOOKIEPOLICYCODE_P_H
#define QCCOOKIEPOLICYCODE_P_H

#include <QString>

namespace QCurl::Internal::cookiepolicy {

inline const QString kApplied                 = QStringLiteral("cookie.applied");
inline const QString kApplyFailedRolledBack   = QStringLiteral("cookie.apply_failed_rolled_back");
inline const QString kClearFailed             = QStringLiteral("cookie.clear_failed");
inline const QString kExportFailed            = QStringLiteral("cookie.export_failed");
inline const QString kExportParseFailed       = QStringLiteral("cookie.export_parse_failed");
inline const QString kHandleInitFailed        = QStringLiteral("cookie.handle_init_failed");
inline const QString kInvalidInput            = QStringLiteral("cookie.invalid_input");
inline const QString kInvalidManager          = QStringLiteral("cookie.invalid_manager");
inline const QString kMultiUnavailable        = QStringLiteral("cookie.multi_unavailable");
inline const QString kOptionApplied           = QStringLiteral("cookie.option_applied");
inline const QString kPersistenceFailed       = QStringLiteral("cookie.persistence_failed");
inline const QString kRequiredOptionFailed    = QStringLiteral("cookie.required_option_failed");
inline const QString kRollbackFailed          = QStringLiteral("cookie.rollback_failed");
inline const QString kShareBusy               = QStringLiteral("cookie.share_busy");
inline const QString kShareContextUnavailable = QStringLiteral("cookie.share_context_unavailable");
inline const QString kShareDisabled           = QStringLiteral("cookie.share_disabled");
inline const QString kShareInitFailed         = QStringLiteral("cookie.share_init_failed");
inline const QString kSnapshotFailed          = QStringLiteral("cookie.snapshot_failed");
inline const QString kStorePoisoned           = QStringLiteral("cookie.store_poisoned");
inline const QString kValidated               = QStringLiteral("cookie.validated");

} // namespace QCurl::Internal::cookiepolicy

#endif // QCCOOKIEPOLICYCODE_P_H
