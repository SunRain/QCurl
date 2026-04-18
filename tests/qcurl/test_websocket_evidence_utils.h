#ifndef QCURL_TEST_WEBSOCKET_EVIDENCE_UTILS_H
#define QCURL_TEST_WEBSOCKET_EVIDENCE_UTILS_H

#include "test_wait_utils.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

namespace QCurl::TestWebSocketEvidenceUtils {

inline QUrl buildCaseUrl(const QString &baseUrl, const QString &path, const QString &caseId)
{
    QUrl url(baseUrl);
    if (!path.isEmpty()) {
        url.setPath(path.startsWith('/') ? path : QStringLiteral("/") + path);
    }

    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("case"), caseId);
    url.setQuery(query);
    return url;
}

template <typename Predicate>
QList<QJsonObject> readJsonlObjects(const QString &jsonlPath, Predicate &&predicate)
{
    QList<QJsonObject> out;
    QFile file(jsonlPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return out;
    }

    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }

        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }

        const QJsonObject object = doc.object();
        if (predicate(object)) {
            out.append(object);
        }
    }

    return out;
}

inline QList<QJsonObject> readAllJsonlObjects(const QString &jsonlPath)
{
    return readJsonlObjects(jsonlPath, [](const QJsonObject &) { return true; });
}

inline QList<QJsonObject> readCaseEventsOnce(const QString &jsonlPath,
                                             const QString &caseId,
                                             const QString &event = {})
{
    return readJsonlObjects(jsonlPath, [&](const QJsonObject &object) {
        if (!event.isEmpty() && object.value(QStringLiteral("event")).toString() != event) {
            return false;
        }
        return object.value(QStringLiteral("case")).toString() == caseId;
    });
}

inline QList<QJsonObject> waitCaseEvents(const QString &jsonlPath,
                                         const QString &caseId,
                                         const QString &event,
                                         int minCount,
                                         int timeoutMs)
{
    QList<QJsonObject> out;
    TestWaitUtils::waitUntil([&]() {
        out = readCaseEventsOnce(jsonlPath, caseId, event);
        return out.size() >= minCount;
    }, timeoutMs, 50);
    return out;
}

inline QList<QJsonObject> readFrameEventsByCaseOnce(const QString &jsonlPath,
                                                    const QString &caseId,
                                                    const QString &direction = {})
{
    return readJsonlObjects(jsonlPath, [&](const QJsonObject &object) {
        if (object.value(QStringLiteral("event")).toString() != QStringLiteral("ws_frame")) {
            return false;
        }
        if (object.value(QStringLiteral("case")).toString() != caseId) {
            return false;
        }
        if (!direction.isEmpty()
            && object.value(QStringLiteral("direction")).toString() != direction) {
            return false;
        }
        return true;
    });
}

inline QList<QJsonObject> waitFrameEventsByCase(const QString &jsonlPath,
                                                const QString &caseId,
                                                int minCount,
                                                int timeoutMs,
                                                const QString &direction = {})
{
    QList<QJsonObject> out;
    TestWaitUtils::waitUntil([&]() {
        out = readFrameEventsByCaseOnce(jsonlPath, caseId, direction);
        return out.size() >= minCount;
    }, timeoutMs, 50);
    return out;
}

inline QString tailForFailure(const QString &jsonlPath, int maxLines = 80)
{
    QFile file(jsonlPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QStringLiteral("(unable to read %1)").arg(jsonlPath);
    }

    QStringList lines;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty()) {
            continue;
        }
        lines.append(line);
        if (lines.size() > maxLines) {
            lines.removeFirst();
        }
    }

    if (lines.isEmpty()) {
        return QStringLiteral("(empty)");
    }
    return lines.join(QLatin1Char('\n'));
}

inline QString verifyHandshakeEvidence(const QString &jsonlPath,
                                       const QString &caseId,
                                       const QString &expectedPath,
                                       bool requireTlsServerStart,
                                       int minCount,
                                       int timeoutMs)
{
    QList<QJsonObject> handshakes;
    bool tlsServerStarted = !requireTlsServerStart;

    TestWaitUtils::waitUntil([&]() {
        const QList<QJsonObject> objects = readAllJsonlObjects(jsonlPath);

        tlsServerStarted = !requireTlsServerStart;
        if (requireTlsServerStart) {
            for (const QJsonObject &object : objects) {
                if (object.value(QStringLiteral("event")).toString()
                    != QStringLiteral("server_start")) {
                    continue;
                }
                if (object.value(QStringLiteral("tls")).toBool()) {
                    tlsServerStarted = true;
                    break;
                }
            }
        }

        handshakes.clear();
        for (const QJsonObject &object : objects) {
            if (object.value(QStringLiteral("event")).toString()
                != QStringLiteral("handshake_ok")) {
                continue;
            }
            if (object.value(QStringLiteral("case")).toString() != caseId) {
                continue;
            }
            if (!expectedPath.isEmpty()
                && object.value(QStringLiteral("path")).toString() != expectedPath) {
                continue;
            }
            handshakes.append(object);
        }

        return tlsServerStarted && handshakes.size() >= minCount;
    }, timeoutMs, 50);

    if (tlsServerStarted && handshakes.size() >= minCount) {
        return {};
    }

    return QStringLiteral(
               "Expected %1 handshake_ok event(s) for case=%2 path=%3 with tls=true. "
               "Observed handshake count=%4, tls_server_start=%5.\nEvidence tail:\n%6")
        .arg(minCount)
        .arg(caseId)
        .arg(expectedPath)
        .arg(handshakes.size())
        .arg(tlsServerStarted ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(tailForFailure(jsonlPath));
}

} // namespace QCurl::TestWebSocketEvidenceUtils

#endif // QCURL_TEST_WEBSOCKET_EVIDENCE_UTILS_H
