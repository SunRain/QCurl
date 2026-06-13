/**
 * @file QCNetworkDiagnosticsValues.cpp
 * @brief 诊断结果值类型实现
 */

#include "QCNetworkDiagnostics.h"

#include <QSharedData>

namespace QCurl {

namespace {

constexpr std::chrono::milliseconds kDefaultTimeout{5000};
constexpr std::chrono::milliseconds kMinTimeout{1};
constexpr std::chrono::seconds kMaxTimeout{60};
constexpr int kDefaultDiagnosticsPort   = 443;
constexpr int kDefaultPingCount         = 4;
constexpr int kDefaultTracerouteMaxHops = 30;
constexpr int kMinTcpPort               = 1;
constexpr int kMaxTcpPort               = 65535;
constexpr int kMinCount                 = 1;
constexpr int kMaxPingCount             = 100;
constexpr int kMaxTracerouteMaxHops     = 255;

bool failOption(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

} // namespace

/// QCNetworkDiagnosticsOptions 的共享负载，保存同步诊断的可调参数。
class QCNetworkDiagnosticsOptionsData : public QSharedData
{
public:
    std::chrono::milliseconds timeout = kDefaultTimeout;
    int port                          = kDefaultDiagnosticsPort;
    int pingCount                     = kDefaultPingCount;
    int tracerouteMaxHops             = kDefaultTracerouteMaxHops;
};

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

QCNetworkDiagnosticsOptions::QCNetworkDiagnosticsOptions()
    : d(new QCNetworkDiagnosticsOptionsData)
{}

QCNetworkDiagnosticsOptions::QCNetworkDiagnosticsOptions(
    const QCNetworkDiagnosticsOptions &other) = default;

QCNetworkDiagnosticsOptions::QCNetworkDiagnosticsOptions(
    QCNetworkDiagnosticsOptions &&other) noexcept = default;

QCNetworkDiagnosticsOptions::~QCNetworkDiagnosticsOptions() = default;

QCNetworkDiagnosticsOptions &QCNetworkDiagnosticsOptions::operator=(
    const QCNetworkDiagnosticsOptions &other) = default;

QCNetworkDiagnosticsOptions &QCNetworkDiagnosticsOptions::operator=(
    QCNetworkDiagnosticsOptions &&other) noexcept = default;

std::chrono::milliseconds QCNetworkDiagnosticsOptions::timeout() const noexcept
{
    return d->timeout;
}

bool QCNetworkDiagnosticsOptions::setTimeout(std::chrono::milliseconds timeout, QString *error)
{
    if (timeout < kMinTimeout || timeout > kMaxTimeout) {
        return failOption(error, QStringLiteral("timeout 必须在 1ms..60s 范围内"));
    }

    d->timeout = timeout;
    return true;
}

int QCNetworkDiagnosticsOptions::port() const noexcept
{
    return d->port;
}

bool QCNetworkDiagnosticsOptions::setPort(int port, QString *error)
{
    if (port < kMinTcpPort || port > kMaxTcpPort) {
        return failOption(error, QStringLiteral("port 必须在 1..65535 范围内"));
    }

    d->port = port;
    return true;
}

int QCNetworkDiagnosticsOptions::pingCount() const noexcept
{
    return d->pingCount;
}

bool QCNetworkDiagnosticsOptions::setPingCount(int count, QString *error)
{
    if (count < kMinCount || count > kMaxPingCount) {
        return failOption(error, QStringLiteral("pingCount 必须在 1..100 范围内"));
    }

    d->pingCount = count;
    return true;
}

int QCNetworkDiagnosticsOptions::tracerouteMaxHops() const noexcept
{
    return d->tracerouteMaxHops;
}

bool QCNetworkDiagnosticsOptions::setTracerouteMaxHops(int hops, QString *error)
{
    if (hops < kMinCount || hops > kMaxTracerouteMaxHops) {
        return failOption(error, QStringLiteral("tracerouteMaxHops 必须在 1..255 范围内"));
    }

    d->tracerouteMaxHops = hops;
    return true;
}

DiagResult::DiagResult()
    : d(new DiagResultData)
{}

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
