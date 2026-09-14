/**
 * @file QCNetworkDiagnosticsConnectivity.cpp
 * @brief 异步 TCP、TLS、HTTP 与综合诊断实现。
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkCancelToken.h"
#include "QCNetworkDiagnostics.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "private/QCNetworkDiagnosticKeys_p.h"
#include "private/QCNetworkDiagnosticsOperation_p.h"

#include <QAbstractEventDispatcher>
#include <QFutureWatcher>
#include <QPointer>
#include <QSharedPointer>
#include <QSslCertificate>
#include <QSslSocket>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include <limits>
#include <utility>

namespace QCurl {

namespace {

using Internal::DiagnosticsOperation;

constexpr int kDefaultHttpPort  = 80;
constexpr int kDefaultHttpsPort = 443;

QFuture<DiagResult> dispatchFailure(const QString &operation, const QString &target)
{
    DiagResult result;
    result.setSuccess(false);
    result.setSummary(QStringLiteral("%1 无法调度: %2").arg(operation, target));
    result.setErrorString(QStringLiteral("DispatchFailed"));
    result.setDetail(QCurl::Internal::diagnostickeys::kTarget, target);
    return Internal::finishedDiagnosticsFuture(std::move(result));
}

bool canDispatch()
{
    return QAbstractEventDispatcher::instance(QThread::currentThread()) != nullptr;
}

QString redactDiagnosticUrl(const QUrl &url)
{
    QUrl redacted(url);
    redacted.setUserInfo(QString());
    redacted.setQuery(QString());
    redacted.setFragment(QString());
    return redacted.toString(QUrl::FullyEncoded);
}

DiagResult sslHandshakeResult(QSslSocket *socket, const QString &host, int port)
{
    const QSslCertificate certificate = socket->peerCertificate();
    DiagResult result;
    result.setSuccess(true);
    result.setSummary(QStringLiteral("SSL 证书有效: %1").arg(host));
    result.setDetail(QCurl::Internal::diagnostickeys::kHost, host);
    result.setDetail(QStringLiteral("port"), port);
    result.setDetail(QStringLiteral("issuer"), certificate.issuerDisplayName());
    result.setDetail(QStringLiteral("subject"), certificate.subjectDisplayName());
    result.setDetail(QStringLiteral("notBefore"), certificate.effectiveDate());
    result.setDetail(QStringLiteral("notAfter"), certificate.expiryDate());
    result.setDetail(QStringLiteral("daysValid"),
                     QDateTime::currentDateTime().daysTo(certificate.expiryDate()));
    result.setDetail(QStringLiteral("tlsVersion"),
                     socket->sessionProtocol() == QSsl::TlsV1_3 ? QStringLiteral("TLSv1.3")
                                                                : QStringLiteral("TLSv1.2"));
    result.setDetail(QStringLiteral("verified"), socket->sslHandshakeErrors().isEmpty());
    return result;
}

DiagResult httpProbeResult(QCNetworkReply *reply, const QString &redactedUrl, qint64 elapsedMs)
{
    DiagResult result;
    result.setDetail(QStringLiteral("url"), redactedUrl);
    result.setDetail(QStringLiteral("totalTime"), elapsedMs);
    result.setDetail(QStringLiteral("finalURL"), redactDiagnosticUrl(reply->url()));
    result.setDetail(QStringLiteral("networkError"), static_cast<int>(reply->error()));
    if (reply->httpStatusCode() > 0) {
        result.setDetail(QStringLiteral("statusCode"), reply->httpStatusCode());
    }
    result.setSuccess(reply->error() == NetworkError::NoError);
    result.setSummary(result.success() ? QStringLiteral("HTTP 探测成功: %1").arg(redactedUrl)
                                       : QStringLiteral("HTTP 探测失败: %1").arg(redactedUrl));
    if (!result.success()) {
        result.setErrorString(reply->errorString());
        result.setDetail(QCurl::Internal::diagnostickeys::kErrorString, result.errorString());
    }
    return result;
}

class DiagnosisSequence final : public QObject
{
    Q_OBJECT

public:
    DiagnosisSequence(DiagnosticsOperation *state, QUrl url, QCNetworkDiagnosticsOptions options)
        : QObject(state)
        , m_state(state)
        , m_url(std::move(url))
        , m_options(std::move(options))
        , m_cancelToken(new QCNetworkCancelToken(this))
    {
        m_result.setTimestamp(QDateTime::currentDateTime());
    }

    void start()
    {
        watch(QCNetworkDiagnostics::resolveDNS(m_url.host(), m_options, m_cancelToken),
              [this](const DiagResult &dns) {
                  m_result.setDetail(QStringLiteral("dns"), Internal::diagResultToVariantMap(dns));
                  if (!dns.success()) {
                      fail(QStringLiteral("dns"), QStringLiteral("诊断失败: DNS 解析失败"), dns);
                      return;
                  }
                  startConnection();
              });
    }

    void cancelSteps() { Q_UNUSED(m_cancelToken->cancel()); }

private:
    Q_DISABLE_COPY_MOVE(DiagnosisSequence)

    template<typename Callback>
    void watch(QFuture<DiagResult> future, Callback callback)
    {
        auto *watcher = new QFutureWatcher<DiagResult>(this);
        QObject::connect(watcher,
                         &QFutureWatcher<DiagResult>::finished,
                         this,
                         [this, watcher, callback = std::move(callback)]() mutable {
                             watcher->deleteLater();
                             if (m_state->isFinished() || watcher->future().resultCount() == 0) {
                                 return;
                             }
                             callback(watcher->future().result());
                         });
        watcher->setFuture(future);
    }

    void startConnection()
    {
        watch(QCNetworkDiagnostics::testConnection(m_url.host(), m_options, m_cancelToken),
              [this](const DiagResult &connection) {
                  m_result.setDetail(QStringLiteral("connection"),
                                     Internal::diagResultToVariantMap(connection));
                  if (!connection.success()) {
                      fail(QStringLiteral("connection"),
                           QStringLiteral("诊断失败: 连接测试失败"),
                           connection);
                      return;
                  }
                  if (m_url.scheme() == QStringLiteral("https")) {
                      startSsl();
                  } else {
                      startHttp();
                  }
              });
    }

    void startSsl()
    {
        watch(QCNetworkDiagnostics::checkSSL(m_url.host(), m_options, m_cancelToken),
              [this](const DiagResult &ssl) {
                  m_result.setDetail(QStringLiteral("ssl"), Internal::diagResultToVariantMap(ssl));
                  m_hasSslWarning = !ssl.success();
                  startHttp();
              });
    }

    void startHttp()
    {
        watch(QCNetworkDiagnostics::probeHTTP(m_url, m_options, m_cancelToken),
              [this](const DiagResult &http) {
                  m_result.setDetail(QStringLiteral("http"), Internal::diagResultToVariantMap(http));
                  if (!http.success()) {
                      fail(QStringLiteral("http"), QStringLiteral("诊断失败: HTTP 探测失败"), http);
                      return;
                  }

                  m_result.setSuccess(true);
                  m_result.setSummary(
                      m_hasSslWarning
                          ? QStringLiteral("综合诊断完成（SSL 警告）: %1").arg(m_url.toString())
                          : QStringLiteral("综合诊断完成: %1").arg(m_url.toString()));
                  m_result.setDetail(QStringLiteral("overallHealth"),
                                     m_hasSslWarning ? QStringLiteral("warning")
                                                     : QStringLiteral("excellent"));
                  m_state->finish(std::move(m_result));
              });
    }

    void fail(const QString &step, const QString &summary, const DiagResult &stepResult)
    {
        m_result.setSuccess(false);
        m_result.setSummary(summary);
        m_result.setErrorString(stepResult.errorString());
        m_result.setDetail(QStringLiteral("failedStep"), step);
        m_result.setDetail(QStringLiteral("overallHealth"), QStringLiteral("error"));
        m_state->finish(std::move(m_result));
    }

    DiagnosticsOperation *m_state;
    QUrl m_url;
    QCNetworkDiagnosticsOptions m_options;
    QCNetworkCancelToken *m_cancelToken;
    DiagResult m_result;
    bool m_hasSslWarning = false;
};

} // namespace

QFuture<DiagResult> QCNetworkDiagnostics::testConnection(const QString &host,
                                                         const QCNetworkDiagnosticsOptions &options,
                                                         QCNetworkCancelToken *cancelToken)
{
    const QString target = QStringLiteral("%1:%2").arg(host).arg(options.port());
    if (!canDispatch()) {
        return dispatchFailure(QStringLiteral("连接测试"), target);
    }

    auto *state = new DiagnosticsOperation(QStringLiteral("连接测试"),
                                           target,
                                           Internal::diagnosticsTimeoutMs(options),
                                           cancelToken);
    const QFuture<DiagResult> future = state->future();
    auto *socket                     = new QTcpSocket(state);
    state->setCancelHandler([socket]() { socket->abort(); });

    QObject::connect(socket, &QTcpSocket::connected, state, [state, socket, host, options]() {
        if (state->isFinished()) {
            return;
        }
        DiagResult result;
        result.setSuccess(true);
        result.setSummary(QStringLiteral("连接成功: %1:%2").arg(host).arg(options.port()));
        result.setDetail(QCurl::Internal::diagnostickeys::kHost, host);
        result.setDetail(QStringLiteral("port"), options.port());
        result.setDetail(QStringLiteral("connected"), true);
        result.setDetail(QStringLiteral("connectDuration"), state->elapsedMs());
        result.setDetail(QCurl::Internal::diagnostickeys::kResolvedIp,
                         socket->peerAddress().toString());
        socket->disconnectFromHost();
        state->finish(std::move(result));
    });
    QObject::connect(socket,
                     &QTcpSocket::errorOccurred,
                     state,
                     [state, socket, host, options](QAbstractSocket::SocketError) {
                         if (state->isFinished()) {
                             return;
                         }
                         DiagResult result;
                         result.setSuccess(false);
                         result.setSummary(
                             QStringLiteral("连接失败: %1:%2").arg(host).arg(options.port()));
                         result.setErrorString(socket->errorString());
                         result.setDetail(QCurl::Internal::diagnostickeys::kHost, host);
                         result.setDetail(QStringLiteral("port"), options.port());
                         result.setDetail(QStringLiteral("connected"), false);
                         state->finish(std::move(result));
                     });
    socket->connectToHost(host, static_cast<quint16>(options.port()));
    return future;
}

QFuture<DiagResult> QCNetworkDiagnostics::checkSSL(const QString &host,
                                                   const QCNetworkDiagnosticsOptions &options,
                                                   QCNetworkCancelToken *cancelToken)
{
    const QString target = QStringLiteral("%1:%2").arg(host).arg(options.port());
    if (!canDispatch()) {
        return dispatchFailure(QStringLiteral("SSL 握手"), target);
    }

    auto *state = new DiagnosticsOperation(QStringLiteral("SSL 握手"),
                                           target,
                                           Internal::diagnosticsTimeoutMs(options),
                                           cancelToken);
    const QFuture<DiagResult> future = state->future();
    auto *socket                     = new QSslSocket(state);
    auto sslErrors                   = QSharedPointer<QStringList>::create();
    state->setCancelHandler([socket]() { socket->abort(); });

    QObject::connect(socket,
                     &QSslSocket::sslErrors,
                     state,
                     [sslErrors](const QList<QSslError> &errors) {
                         sslErrors->clear();
                         for (const QSslError &error : errors) {
                             sslErrors->append(error.errorString());
                         }
                     });
    QObject::connect(socket, &QSslSocket::encrypted, state, [state, socket, host, options]() {
        if (state->isFinished()) {
            return;
        }
        auto result = sslHandshakeResult(socket, host, options.port());
        socket->disconnectFromHost();
        state->finish(std::move(result));
    });
    QObject::connect(socket,
                     &QSslSocket::errorOccurred,
                     state,
                     [state, socket, sslErrors, host](QAbstractSocket::SocketError) {
                         if (state->isFinished()) {
                             return;
                         }
                         DiagResult result;
                         result.setSuccess(false);
                         result.setSummary(QStringLiteral("SSL 握手失败: %1").arg(host));
                         result.setErrorString(socket->errorString());
                         if (!sslErrors->isEmpty()) {
                             result.setDetail(QStringLiteral("sslErrors"), *sslErrors);
                         }
                         state->finish(std::move(result));
                     });
    socket->connectToHostEncrypted(host, static_cast<quint16>(options.port()));
    return future;
}

QFuture<DiagResult> QCNetworkDiagnostics::probeHTTP(const QUrl &url,
                                                    const QCNetworkDiagnosticsOptions &options,
                                                    QCNetworkCancelToken *cancelToken)
{
    const QString redactedUrl = redactDiagnosticUrl(url);
    if (!canDispatch()) {
        return dispatchFailure(QStringLiteral("HTTP 探测"), redactedUrl);
    }

    auto *state = new DiagnosticsOperation(QStringLiteral("HTTP 探测"),
                                           redactedUrl,
                                           Internal::diagnosticsTimeoutMs(options),
                                           cancelToken);
    const QFuture<DiagResult> future = state->future();
    auto *manager                    = new QCNetworkAccessManager(state);
    QCNetworkRequest request(url);
    request.setTimeout(options.timeout());
    request.setConnectTimeout(options.timeout());

    QPointer<QCNetworkReply> reply = manager->get(request);
    if (!reply) {
        state->fail(QStringLiteral("HTTP 探测失败: %1").arg(redactedUrl),
                    QStringLiteral("QCurl reply 创建失败"));
        return future;
    }

    state->setCancelHandler([reply]() {
        if (reply) {
            reply->cancel();
        }
    });
    QObject::connect(manager, &QObject::destroyed, state, [state, redactedUrl]() {
        if (!state->isFinished()) {
            state->fail(QStringLiteral("HTTP 探测失败: %1").arg(redactedUrl),
                        QStringLiteral("OwnerDestroyed"));
        }
    });

    const auto collectResult = [state, reply, redactedUrl]() {
        if (state->isFinished() || !reply) {
            return;
        }
        auto result = httpProbeResult(reply.data(), redactedUrl, state->elapsedMs());
        state->finish(std::move(result));
    };
    QObject::connect(reply, &QCNetworkReply::finished, state, collectResult);
    if (reply->isFinished()) {
        QTimer::singleShot(0, state, collectResult);
    }
    return future;
}

QFuture<DiagResult> QCNetworkDiagnostics::diagnose(const QUrl &url,
                                                   const QCNetworkDiagnosticsOptions &options,
                                                   QCNetworkCancelToken *cancelToken)
{
    const QString redactedUrl = redactDiagnosticUrl(url);
    if (!canDispatch()) {
        return dispatchFailure(QStringLiteral("综合诊断"), redactedUrl);
    }

    QCNetworkDiagnosticsOptions stepOptions = options;
    const bool https                        = url.scheme() == QStringLiteral("https");
    QString optionError;
    if (!stepOptions.setPort(url.port(https ? kDefaultHttpsPort : kDefaultHttpPort), &optionError)) {
        DiagResult result;
        result.setSuccess(false);
        result.setSummary(QStringLiteral("诊断失败: URL 端口无效"));
        result.setErrorString(optionError);
        result.setDetail(QStringLiteral("overallHealth"), QStringLiteral("error"));
        return Internal::finishedDiagnosticsFuture(std::move(result));
    }

    const qint64 totalTimeout = static_cast<qint64>(Internal::diagnosticsTimeoutMs(options))
                                * (https ? 4 : 3);
    const int boundedTimeout  = static_cast<int>(
        std::min<qint64>(totalTimeout, std::numeric_limits<int>::max()));
    auto *state                      = new DiagnosticsOperation(QStringLiteral("综合诊断"),
                                                                redactedUrl,
                                                                boundedTimeout,
                                                                cancelToken);
    const QFuture<DiagResult> future = state->future();
    auto *sequence                   = new DiagnosisSequence(state, url, stepOptions);
    state->setCancelHandler([sequence]() { sequence->cancelSteps(); });
    sequence->start();
    return future;
}

} // namespace QCurl

#include "QCNetworkDiagnosticsConnectivity.moc"
