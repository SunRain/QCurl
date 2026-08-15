/**
 * @file
 * @brief 声明 cookie async bridge 的值结果。
 */

#ifndef QCCOOKIEASYNCRESULT_H
#define QCCOOKIEASYNCRESULT_H

#include "QCCookie.h"
#include "QCGlobal.h"

#include <QList>
#include <QMetaType>
#include <QSharedDataPointer>
#include <QString>

namespace QCurl {

/** 异步 cookie 调用完成时的稳定错误分类。 */
enum class QCCookieAsyncError {
    None,             ///< 操作成功。
    ManagerDestroyed, ///< manager 在 owner-thread command 执行前销毁。
    DispatchFailed,   ///< command 无法投递到 manager owner thread。
    Cancelled,        ///< Future 在 command 执行前被取消。
    BusinessError,    ///< Cookie store 策略或 libcurl required option 失败。
};

class QCCookieExportResultData;
class QCCookieOperationResultData;

/**
 * @brief 异步 manager cookie 修改操作的结果值类型。
 *
 * @note 错误生命周期：结果构造后不再变化。成功时 `errorCode()` 为 `None`，且
 * `policyCode()` 与 `error()` 为空；失败时 `errorCode()` 是权威分类，文本仅用于诊断。
 */
class QCURL_EXPORT QCCookieOperationResult
{
public:
    QCCookieOperationResult();
    QCCookieOperationResult(const QCCookieOperationResult &other);
    QCCookieOperationResult(QCCookieOperationResult &&other) noexcept;
    ~QCCookieOperationResult();

    QCCookieOperationResult &operator=(const QCCookieOperationResult &other);
    QCCookieOperationResult &operator=(QCCookieOperationResult &&other) noexcept;

    [[nodiscard]] static QCCookieOperationResult success();
    /** 创建带稳定分类和可选 policy code 的失败结果。 */
    [[nodiscard]] static QCCookieOperationResult failure(QCCookieAsyncError code,
                                                         const QString &error,
                                                         const QString &policyCode = {});
    [[nodiscard]] static QCCookieOperationResult failure(const QString &error);

    [[nodiscard]] bool isSuccess() const noexcept;
    /// 返回调用生命周期或业务失败分类。
    [[nodiscard]] QCCookieAsyncError errorCode() const noexcept;
    /// 返回 cookie store 业务策略码；生命周期失败时为空。
    [[nodiscard]] QString policyCode() const;
    [[nodiscard]] QString error() const;

private:
    QSharedDataPointer<QCCookieOperationResultData> d;
};

/**
 * @brief 异步 manager cookie 导出操作的结果值类型。
 *
 * 成功返回的空 cookie 列表表示没有匹配项，不表示失败。错误生命周期与
 * `QCCookieOperationResult` 相同。
 */
class QCURL_EXPORT QCCookieExportResult
{
public:
    QCCookieExportResult();
    QCCookieExportResult(const QCCookieExportResult &other);
    QCCookieExportResult(QCCookieExportResult &&other) noexcept;
    ~QCCookieExportResult();

    QCCookieExportResult &operator=(const QCCookieExportResult &other);
    QCCookieExportResult &operator=(QCCookieExportResult &&other) noexcept;

    [[nodiscard]] static QCCookieExportResult success(const QList<QCCookie> &cookies);
    /** 创建带稳定分类和可选 policy code 的导出失败结果。 */
    [[nodiscard]] static QCCookieExportResult failure(QCCookieAsyncError code,
                                                      const QString &error,
                                                      const QString &policyCode = {});
    [[nodiscard]] static QCCookieExportResult failure(const QString &error);

    [[nodiscard]] bool isSuccess() const noexcept;
    /// 返回调用生命周期或业务失败分类。
    [[nodiscard]] QCCookieAsyncError errorCode() const noexcept;
    /// 返回 cookie store 业务策略码；生命周期失败时为空。
    [[nodiscard]] QString policyCode() const;
    [[nodiscard]] QList<QCCookie> cookies() const;
    [[nodiscard]] QString error() const;

private:
    QSharedDataPointer<QCCookieExportResultData> d;
};

} // namespace QCurl

Q_DECLARE_METATYPE(QCurl::QCCookieAsyncError)
Q_DECLARE_METATYPE(QCurl::QCCookieOperationResult)
Q_DECLARE_METATYPE(QCurl::QCCookieExportResult)

#endif // QCCOOKIEASYNCRESULT_H
