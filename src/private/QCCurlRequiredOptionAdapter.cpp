#include "QCCurlRequiredOptionAdapter_p.h"
#include "private/QCCookiePolicyCode_p.h"

namespace QCurl::Internal {

QByteArray CookieOptionAdapter::stageName(CookieOptionStage stage)
{
    switch (stage) {
        case CookieOptionStage::Setup:
            return QByteArrayLiteral("setup");
        case CookieOptionStage::Snapshot:
            return QByteArrayLiteral("snapshot");
        case CookieOptionStage::Apply:
            return QByteArrayLiteral("apply");
        case CookieOptionStage::Rollback:
            return QByteArrayLiteral("rollback");
        case CookieOptionStage::Persist:
            return QByteArrayLiteral("persist");
        case CookieOptionStage::Clear:
            return QByteArrayLiteral("clear");
        case CookieOptionStage::Export:
            return QByteArrayLiteral("export");
    }
    return QByteArrayLiteral("unknown");
}

QByteArray CookieOptionAdapter::stageKey(CookieOptionStage stage, const QByteArray &name)
{
    return stageName(stage) + ':' + name;
}

bool CookieOptionAdapter::shouldFailEasy(CookieOptionStage stage,
                                         const QByteArray &name,
                                         int occurrence)
{
#ifdef QCURL_ENABLE_TEST_HOOKS
    const QList<QByteArray> points = qgetenv("QCURL_TEST_FORCE_COOKIE_OPTION_ERROR").split(',');
    const QByteArray base          = stageKey(stage, name);
    const QByteArray indexed       = base + ':' + QByteArray::number(occurrence);
    for (const QByteArray &point : points) {
        const QByteArray trimmed = point.trimmed();
        if (trimmed == base || trimmed == indexed) {
            return true;
        }
    }
#else
    Q_UNUSED(stage)
    Q_UNUSED(name)
    Q_UNUSED(occurrence)
#endif
    return false;
}

bool CookieOptionAdapter::shouldFailShare(const QByteArray &name, const QByteArray &detail)
{
#ifdef QCURL_ENABLE_TEST_HOOKS
    const QList<QByteArray> points = qgetenv("QCURL_TEST_FORCE_SHARE_OPTION_ERROR").split(',');
    const QByteArray detailed      = detail.isEmpty() ? name : name + ':' + detail;
    for (const QByteArray &point : points) {
        const QByteArray trimmed = point.trimmed();
        if (trimmed == name || trimmed == detailed) {
            return true;
        }
    }
#else
    Q_UNUSED(name)
    Q_UNUSED(detail)
#endif
    return false;
}

RequiredOptionResult CookieOptionAdapter::easyResult(CURLcode code, const QByteArray &name)
{
    RequiredOptionResult result;
    result.curlCode   = code;
    result.optionName = name;
    result.policyCode = code == CURLE_OK ? QCurl::Internal::cookiepolicy::kOptionApplied
                                         : QCurl::Internal::cookiepolicy::kRequiredOptionFailed;
    if (code != CURLE_OK) {
        result.message = QStringLiteral("required option %1 失败（%2）")
                             .arg(QString::fromLatin1(name),
                                  QString::fromUtf8(curl_easy_strerror(code)));
    }
    return result;
}

RequiredOptionResult CookieOptionAdapter::shareResult(CURLSHcode code,
                                                      const QByteArray &name,
                                                      const QByteArray &detail)
{
    RequiredOptionResult result;
    result.shareCode  = code;
    result.optionName = name;
    result.policyCode = code == CURLSHE_OK ? QCurl::Internal::cookiepolicy::kOptionApplied
                                           : QCurl::Internal::cookiepolicy::kRequiredOptionFailed;
    if (code != CURLSHE_OK) {
        const QString label = detail.isEmpty()
                                  ? QString::fromLatin1(name)
                                  : QStringLiteral("%1:%2").arg(QString::fromLatin1(name),
                                                                QString::fromLatin1(detail));
        result.message      = QStringLiteral("required option %1 失败（%2）")
                                  .arg(label, QString::fromUtf8(curl_share_strerror(code)));
    }
    return result;
}

} // namespace QCurl::Internal
