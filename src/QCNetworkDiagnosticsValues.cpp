/**
 * @file QCNetworkDiagnosticsValues.cpp
 * @brief 诊断结果值类型实现
 */

#include "QCNetworkDiagnostics.h"

#include <QSharedData>

namespace QCurl {

/// DiagResult 的共享负载；details 字段不承诺稳定 schema。
class DiagResultData : public QSharedData
{
public:
    bool success = false;
    QString summary;
    QVariantMap details;
    qint64 durationMs = 0;
    QDateTime timestamp;
    QString errorString;
};

DiagResult::DiagResult()
    : d(new DiagResultData)
{
}

DiagResult::DiagResult(const DiagResult &other) = default;

DiagResult::DiagResult(DiagResult &&other) noexcept = default;

DiagResult::~DiagResult() = default;

DiagResult &DiagResult::operator=(const DiagResult &other) = default;

DiagResult &DiagResult::operator=(DiagResult &&other) noexcept = default;

bool DiagResult::success() const noexcept
{
    return d->success;
}

void DiagResult::setSuccess(bool success) noexcept
{
    d->success = success;
}

QString DiagResult::summary() const
{
    return d->summary;
}

void DiagResult::setSummary(const QString &summary)
{
    d->summary = summary;
}

QVariantMap DiagResult::details() const
{
    return d->details;
}

void DiagResult::setDetails(const QVariantMap &details)
{
    d->details = details;
}

void DiagResult::setDetail(const QString &key, const QVariant &value)
{
    d->details.insert(key, value);
}

qint64 DiagResult::durationMs() const noexcept
{
    return d->durationMs;
}

void DiagResult::setDurationMs(qint64 durationMs) noexcept
{
    d->durationMs = durationMs;
}

QDateTime DiagResult::timestamp() const
{
    return d->timestamp;
}

void DiagResult::setTimestamp(const QDateTime &timestamp)
{
    d->timestamp = timestamp;
}

QString DiagResult::errorString() const
{
    return d->errorString;
}

void DiagResult::setErrorString(const QString &errorString)
{
    d->errorString = errorString;
}

QString DiagResult::toString() const
{
    QString result;
    result += success() ? QStringLiteral("✅ ") : QStringLiteral("❌ ");
    result += summary() + QStringLiteral("\n");

    const QVariantMap detailMap = details();
    if (!detailMap.isEmpty()) {
        result += QStringLiteral("详细信息:\n");
        for (auto it = detailMap.constBegin(); it != detailMap.constEnd(); ++it) {
            result += QStringLiteral("  %1: %2\n").arg(it.key(), it.value().toString());
        }
    }

    result += QStringLiteral("耗时: %1ms\n").arg(durationMs());

    if (!success() && !errorString().isEmpty()) {
        result += QStringLiteral("错误: %1\n").arg(errorString());
    }

    return result;
}

} // namespace QCurl
