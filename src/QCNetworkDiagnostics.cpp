/**
 * @file QCNetworkDiagnostics.cpp
 * @brief 异步 DNS 诊断实现。
 */

#include "QCNetworkDiagnostics.h"

#include "private/QCNetworkDiagnosticKeys_p.h"
#include "private/QCNetworkDiagnosticsOperation_p.h"

#include <QAbstractEventDispatcher>
#include <QHostAddress>
#include <QHostInfo>
#include <QThread>

#include <utility>

namespace QCurl {

namespace {

using Internal::DiagnosticsOperation;

QFuture<DiagResult> dispatchFailure(const QString &operation, const QString &target)
{
    DiagResult result;
    result.setSuccess(false);
    result.setSummary(QStringLiteral("%1 无法调度: %2").arg(operation, target));
    result.setErrorString(QStringLiteral("DispatchFailed"));
    result.setDetail(QCurl::Internal::diagnostickeys::kTarget, target);
    return Internal::finishedDiagnosticsFuture(std::move(result));
}

QFuture<DiagResult> startHostLookup(const QString &target,
                                    const QCNetworkDiagnosticsOptions &options,
                                    QCNetworkCancelToken *cancelToken,
                                    bool reverse)
{
    const QString operation = reverse ? QStringLiteral("反向 DNS 解析")
                                      : QStringLiteral("DNS 解析");
    if (!QAbstractEventDispatcher::instance(QThread::currentThread())) {
        return dispatchFailure(operation, target);
    }

    auto *state = new DiagnosticsOperation(operation,
                                           target,
                                           Internal::diagnosticsTimeoutMs(options),
                                           cancelToken);
    const QFuture<DiagResult> future = state->future();

    const int lookupId
        = QHostInfo::lookupHost(target, state, [state, target, reverse](QHostInfo info) {
              if (state->isFinished()) {
                  return;
              }

              DiagResult result;
              const bool valid = info.error() == QHostInfo::NoError
                                 && (!reverse || !info.hostName().isEmpty());
              result.setSuccess(valid);
              result.setDetail(reverse ? QStringLiteral("ip") : QStringLiteral("hostname"), target);

              if (!valid) {
                  result.setSummary(QStringLiteral("%1失败: %2")
                                        .arg(reverse ? QStringLiteral("反向 DNS 解析")
                                                     : QStringLiteral("DNS 解析"),
                                             target));
                  result.setErrorString(info.errorString().isEmpty()
                                            ? QStringLiteral("No host name")
                                            : info.errorString());
                  state->finish(std::move(result));
                  return;
              }

              if (reverse) {
                  result.setSummary(
                      QStringLiteral("反向 DNS 解析成功: %1 -> %2").arg(target, info.hostName()));
                  result.setDetail(QStringLiteral("hostname"), info.hostName());
              } else {
                  QStringList ipv4;
                  QStringList ipv6;
                  for (const QHostAddress &address : info.addresses()) {
                      if (address.protocol() == QAbstractSocket::IPv4Protocol) {
                          ipv4.append(address.toString());
                      } else if (address.protocol() == QAbstractSocket::IPv6Protocol) {
                          ipv6.append(address.toString());
                      }
                  }
                  result.setSummary(QStringLiteral("DNS 解析成功: %1").arg(target));
                  result.setDetail(QStringLiteral("ipv4"), ipv4);
                  result.setDetail(QStringLiteral("ipv6"), ipv6);
              }
              result.setDetail(QStringLiteral("resolveDuration"), state->elapsedMs());
              state->finish(std::move(result));
          });

    state->setCancelHandler([lookupId]() { QHostInfo::abortHostLookup(lookupId); });
    return future;
}

} // namespace

QFuture<DiagResult> QCNetworkDiagnostics::resolveDNS(const QString &hostname,
                                                     const QCNetworkDiagnosticsOptions &options,
                                                     QCNetworkCancelToken *cancelToken)
{
    return startHostLookup(hostname, options, cancelToken, false);
}

QFuture<DiagResult> QCNetworkDiagnostics::reverseDNS(const QString &ip,
                                                     const QCNetworkDiagnosticsOptions &options,
                                                     QCNetworkCancelToken *cancelToken)
{
    return startHostLookup(ip, options, cancelToken, true);
}

} // namespace QCurl
