// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkMockHandler.h"

#include "QCNetworkHttpMethod.h"

#include <QMutexLocker>

#include <algorithm>

namespace QCurl {

QCNetworkMockHandler::QCNetworkMockHandler()  = default;
QCNetworkMockHandler::~QCNetworkMockHandler() = default;

void QCNetworkMockHandler::mockResponse(const QUrl &url, const QByteArray &response, int statusCode)
{
    QMutexLocker locker(&m_mutex);
    // 兼容旧 API：url-only 作为“Any method”
    MockData data;
    data.response   = response;
    data.statusCode = statusCode;
    data.isError    = false;
    replaceSequence(m_sequences[makeKey(url)], data);
}

void QCNetworkMockHandler::mockError(const QUrl &url, NetworkError error)
{
    QMutexLocker locker(&m_mutex);
    // 兼容旧 API：url-only 作为“Any method”
    MockData data;
    data.error   = error;
    data.isError = true;
    replaceSequence(m_sequences[makeKey(url)], data);
}

void QCNetworkMockHandler::mockResponse(HttpMethod method,
                                        const QUrl &url,
                                        const QByteArray &response,
                                        int statusCode,
                                        const QMap<QByteArray, QByteArray> &headers)
{
    QMutexLocker locker(&m_mutex);
    MockData data;
    data.response   = response;
    data.statusCode = statusCode;
    data.headers    = headers;
    data.isError    = false;
    replaceSequence(m_sequences[makeKey(method, url)], data);
}

void QCNetworkMockHandler::mockResponse(HttpMethod method,
                                        const QUrl &url,
                                        const QByteArray &response,
                                        int statusCode,
                                        const QMap<QByteArray, QByteArray> &headers,
                                        const QByteArray &rawHeaderData)
{
    QMutexLocker locker(&m_mutex);
    MockData data;
    data.response      = response;
    data.statusCode    = statusCode;
    data.headers       = headers;
    data.rawHeaderData = rawHeaderData;
    data.isError       = false;
    replaceSequence(m_sequences[makeKey(method, url)], data);
}

void QCNetworkMockHandler::enqueueResponse(HttpMethod method,
                                           const QUrl &url,
                                           const QByteArray &response,
                                           int statusCode,
                                           const QMap<QByteArray, QByteArray> &headers)
{
    QMutexLocker locker(&m_mutex);
    MockData data;
    data.response   = response;
    data.statusCode = statusCode;
    data.headers    = headers;
    data.isError    = false;
    appendSequence(m_sequences[makeKey(method, url)], data);
}

void QCNetworkMockHandler::enqueueResponse(HttpMethod method,
                                           const QUrl &url,
                                           const QByteArray &response,
                                           int statusCode,
                                           const QMap<QByteArray, QByteArray> &headers,
                                           const QByteArray &rawHeaderData)
{
    QMutexLocker locker(&m_mutex);
    MockData data;
    data.response      = response;
    data.statusCode    = statusCode;
    data.headers       = headers;
    data.rawHeaderData = rawHeaderData;
    data.isError       = false;
    appendSequence(m_sequences[makeKey(method, url)], data);
}

void QCNetworkMockHandler::mockError(HttpMethod method,
                                     const QUrl &url,
                                     NetworkError error,
                                     const QMap<QByteArray, QByteArray> &headers)
{
    QMutexLocker locker(&m_mutex);
    MockData data;
    data.error   = error;
    data.headers = headers;
    data.isError = true;
    replaceSequence(m_sequences[makeKey(method, url)], data);
}

void QCNetworkMockHandler::enqueueError(HttpMethod method,
                                        const QUrl &url,
                                        NetworkError error,
                                        const QMap<QByteArray, QByteArray> &headers)
{
    QMutexLocker locker(&m_mutex);
    MockData data;
    data.error   = error;
    data.headers = headers;
    data.isError = true;
    appendSequence(m_sequences[makeKey(method, url)], data);
}

void QCNetworkMockHandler::setGlobalDelay(int msecs)
{
    QMutexLocker locker(&m_mutex);
    m_globalDelay = msecs;
}

int QCNetworkMockHandler::globalDelay() const
{
    QMutexLocker locker(&m_mutex);
    return m_globalDelay;
}

bool QCNetworkMockHandler::hasMock(const QUrl &url) const
{
    QMutexLocker locker(&m_mutex);
    const QString anyKey = makeKey(url);
    if (m_sequences.contains(anyKey)) {
        return true;
    }

    const QString suffix = QStringLiteral("|") + url.toString();
    for (auto it = m_sequences.cbegin(); it != m_sequences.cend(); ++it) {
        if (it.key().endsWith(suffix)) {
            return true;
        }
    }
    return false;
}

bool QCNetworkMockHandler::hasMock(HttpMethod method, const QUrl &url) const
{
    QMutexLocker locker(&m_mutex);
    if (m_sequences.contains(makeKey(method, url))) {
        return true;
    }
    return m_sequences.contains(makeKey(url));
}

QByteArray QCNetworkMockHandler::getMockResponse(const QUrl &url, int &statusCode) const
{
    QMutexLocker locker(&m_mutex);
    const QString anyKey = makeKey(url);
    if (m_sequences.contains(anyKey)) {
        const auto &seq = m_sequences.value(anyKey);
        if (!seq.items.isEmpty()) {
            const auto &data = seq.items.first();
            statusCode       = data.statusCode;
            return data.response;
        }
    }

    const QString suffix = QStringLiteral("|") + url.toString();
    for (auto it = m_sequences.cbegin(); it != m_sequences.cend(); ++it) {
        if (!it.key().endsWith(suffix)) {
            continue;
        }
        const auto &seq = it.value();
        if (!seq.items.isEmpty()) {
            const auto &data = seq.items.first();
            statusCode       = data.statusCode;
            return data.response;
        }
    }
    statusCode = 0;
    return QByteArray();
}

NetworkError QCNetworkMockHandler::getMockError(const QUrl &url) const
{
    QMutexLocker locker(&m_mutex);
    const QString anyKey = makeKey(url);
    if (m_sequences.contains(anyKey)) {
        const auto &seq = m_sequences.value(anyKey);
        if (!seq.items.isEmpty()) {
            const auto &data = seq.items.first();
            if (data.isError) {
                return data.error;
            }
        }
    }
    return NetworkError::NoError;
}

bool QCNetworkMockHandler::isErrorMock(const QUrl &url) const
{
    QMutexLocker locker(&m_mutex);
    const QString anyKey = makeKey(url);
    if (m_sequences.contains(anyKey)) {
        const auto &seq = m_sequences.value(anyKey);
        if (!seq.items.isEmpty()) {
            return seq.items.first().isError;
        }
    }
    return false;
}

bool QCNetworkMockHandler::consumeMock(HttpMethod method, const QUrl &url, MockData &out)
{
    QMutexLocker locker(&m_mutex);
    const QString methodKey = makeKey(method, url);
    const QString anyKey    = makeKey(url);

    auto it = m_sequences.find(methodKey);
    if (it == m_sequences.end()) {
        it = m_sequences.find(anyKey);
    }
    if (it == m_sequences.end()) {
        return false;
    }

    MockSequence &seq = it.value();
    if (seq.items.isEmpty()) {
        return false;
    }

    const int lastIndex = static_cast<int>(seq.items.size() - 1);
    const int index     = std::min(seq.cursor, lastIndex);
    out                 = seq.items.at(index);
    if (seq.cursor < lastIndex) {
        seq.cursor += 1;
    }
    return true;
}

void QCNetworkMockHandler::setCaptureEnabled(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    m_captureEnabled = enabled;
}

bool QCNetworkMockHandler::captureEnabled() const
{
    QMutexLocker locker(&m_mutex);
    return m_captureEnabled;
}

void QCNetworkMockHandler::setCaptureBodyPreviewLimit(int bytes)
{
    QMutexLocker locker(&m_mutex);
    m_captureBodyPreviewLimitBytes = bytes < 0 ? 0 : bytes;
}

int QCNetworkMockHandler::captureBodyPreviewLimit() const
{
    QMutexLocker locker(&m_mutex);
    return m_captureBodyPreviewLimitBytes;
}

void QCNetworkMockHandler::recordRequest(const CapturedRequest &request)
{
    QMutexLocker locker(&m_mutex);
    if (!m_captureEnabled) {
        return;
    }
    m_capturedRequests.append(request);
}

QList<QCNetworkMockHandler::CapturedRequest> QCNetworkMockHandler::capturedRequests() const
{
    QMutexLocker locker(&m_mutex);
    return m_capturedRequests;
}

QList<QCNetworkMockHandler::CapturedRequest> QCNetworkMockHandler::takeCapturedRequests()
{
    QMutexLocker locker(&m_mutex);
    QList<CapturedRequest> out = m_capturedRequests;
    m_capturedRequests.clear();
    return out;
}

void QCNetworkMockHandler::clearCapturedRequests()
{
    QMutexLocker locker(&m_mutex);
    m_capturedRequests.clear();
}

void QCNetworkMockHandler::clear()
{
    QMutexLocker locker(&m_mutex);
    m_sequences.clear();
    m_capturedRequests.clear();
}

QString QCNetworkMockHandler::makeKey(const QUrl &url)
{
    return QStringLiteral("-1|") + url.toString();
}

QString QCNetworkMockHandler::makeKey(HttpMethod method, const QUrl &url)
{
    return QString::number(static_cast<int>(method)) + QStringLiteral("|") + url.toString();
}

void QCNetworkMockHandler::replaceSequence(MockSequence &seq, const MockData &item)
{
    seq.items.clear();
    seq.items.append(item);
    seq.cursor = 0;
}

void QCNetworkMockHandler::appendSequence(MockSequence &seq, const MockData &item)
{
    seq.items.append(item);
    // 注意：cursor 不回退；当 items 变长时，下次 consume 会继续从当前 cursor 开始。
    if (seq.cursor < 0) {
        seq.cursor = 0;
    }
}

} // namespace QCurl
