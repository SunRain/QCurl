// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkMockHandler_p.h"

#include <QMutexLocker>

#include <algorithm>

namespace QCurl {

namespace {

void replaceSequence(QCNetworkMockSequence &seq, const Internal::QCNetworkMockData &item);
void appendSequence(QCNetworkMockSequence &seq, const Internal::QCNetworkMockData &item);

} // namespace

QCNetworkMockHandler::QCNetworkMockHandler()
    : d_ptr(new QCNetworkMockHandlerPrivate)
{}

QCNetworkMockHandler::~QCNetworkMockHandler() = default;

void QCNetworkMockHandler::mockResponse(HttpMethod method,
                                        const QUrl &url,
                                        const QByteArray &response,
                                        int statusCode,
                                        const QMap<QByteArray, QByteArray> &headers)
{
    QMutexLocker locker(&d_ptr->mutex);
    Internal::QCNetworkMockData data;
    data.response   = response;
    data.statusCode = statusCode;
    data.headers    = headers;
    data.isError    = false;
    replaceSequence(d_ptr->sequences[makeKey(method, url)], data);
}

void QCNetworkMockHandler::mockResponse(HttpMethod method,
                                        const QUrl &url,
                                        const QByteArray &response,
                                        int statusCode,
                                        const QMap<QByteArray, QByteArray> &headers,
                                        const QByteArray &rawHeaderData)
{
    QMutexLocker locker(&d_ptr->mutex);
    Internal::QCNetworkMockData data;
    data.response      = response;
    data.statusCode    = statusCode;
    data.headers       = headers;
    data.rawHeaderData = rawHeaderData;
    data.isError       = false;
    replaceSequence(d_ptr->sequences[makeKey(method, url)], data);
}

void QCNetworkMockHandler::enqueueResponse(HttpMethod method,
                                           const QUrl &url,
                                           const QByteArray &response,
                                           int statusCode,
                                           const QMap<QByteArray, QByteArray> &headers)
{
    QMutexLocker locker(&d_ptr->mutex);
    Internal::QCNetworkMockData data;
    data.response   = response;
    data.statusCode = statusCode;
    data.headers    = headers;
    data.isError    = false;
    appendSequence(d_ptr->sequences[makeKey(method, url)], data);
}

void QCNetworkMockHandler::enqueueResponse(HttpMethod method,
                                           const QUrl &url,
                                           const QByteArray &response,
                                           int statusCode,
                                           const QMap<QByteArray, QByteArray> &headers,
                                           const QByteArray &rawHeaderData)
{
    QMutexLocker locker(&d_ptr->mutex);
    Internal::QCNetworkMockData data;
    data.response      = response;
    data.statusCode    = statusCode;
    data.headers       = headers;
    data.rawHeaderData = rawHeaderData;
    data.isError       = false;
    appendSequence(d_ptr->sequences[makeKey(method, url)], data);
}

void QCNetworkMockHandler::mockError(HttpMethod method,
                                     const QUrl &url,
                                     NetworkError error,
                                     const QMap<QByteArray, QByteArray> &headers)
{
    QMutexLocker locker(&d_ptr->mutex);
    Internal::QCNetworkMockData data;
    data.error   = error;
    data.headers = headers;
    data.isError = true;
    replaceSequence(d_ptr->sequences[makeKey(method, url)], data);
}

void QCNetworkMockHandler::enqueueError(HttpMethod method,
                                        const QUrl &url,
                                        NetworkError error,
                                        const QMap<QByteArray, QByteArray> &headers)
{
    QMutexLocker locker(&d_ptr->mutex);
    Internal::QCNetworkMockData data;
    data.error   = error;
    data.headers = headers;
    data.isError = true;
    appendSequence(d_ptr->sequences[makeKey(method, url)], data);
}

void QCNetworkMockHandler::setGlobalDelay(int msecs)
{
    QMutexLocker locker(&d_ptr->mutex);
    d_ptr->globalDelay = msecs;
}

int QCNetworkMockHandler::globalDelay() const
{
    QMutexLocker locker(&d_ptr->mutex);
    return d_ptr->globalDelay;
}

bool QCNetworkMockHandler::hasMock(HttpMethod method, const QUrl &url) const
{
    QMutexLocker locker(&d_ptr->mutex);
    return d_ptr->sequences.contains(makeKey(method, url));
}

QByteArray QCNetworkMockHandler::getMockResponse(HttpMethod method,
                                                 const QUrl &url,
                                                 int &statusCode) const
{
    QMutexLocker locker(&d_ptr->mutex);
    const QString key = makeKey(method, url);
    if (d_ptr->sequences.contains(key)) {
        const auto &seq = d_ptr->sequences.value(key);
        if (!seq.items.isEmpty()) {
            const auto &data = seq.items.first();
            statusCode       = data.statusCode;
            return data.response;
        }
    }

    statusCode = 0;
    return QByteArray();
}

NetworkError QCNetworkMockHandler::getMockError(HttpMethod method, const QUrl &url) const
{
    QMutexLocker locker(&d_ptr->mutex);
    const QString key = makeKey(method, url);
    if (d_ptr->sequences.contains(key)) {
        const auto &seq = d_ptr->sequences.value(key);
        if (!seq.items.isEmpty()) {
            const auto &data = seq.items.first();
            if (data.isError) {
                return data.error;
            }
        }
    }
    return NetworkError::NoError;
}

bool QCNetworkMockHandler::isErrorMock(HttpMethod method, const QUrl &url) const
{
    QMutexLocker locker(&d_ptr->mutex);
    const QString key = makeKey(method, url);
    if (d_ptr->sequences.contains(key)) {
        const auto &seq = d_ptr->sequences.value(key);
        if (!seq.items.isEmpty()) {
            return seq.items.first().isError;
        }
    }
    return false;
}

bool QCNetworkMockHandler::consumeMock(HttpMethod method,
                                       const QUrl &url,
                                       Internal::QCNetworkMockData &out)
{
    QMutexLocker locker(&d_ptr->mutex);
    const QString methodKey = makeKey(method, url);

    auto it = d_ptr->sequences.find(methodKey);
    if (it == d_ptr->sequences.end()) {
        return false;
    }

    QCNetworkMockSequence &seq = it.value();
    if (seq.items.isEmpty()) {
        return false;
    }

    const int lastIndex = static_cast<int>(seq.items.size() - 1);
    // 序列耗尽后复用最后一条，便于测试重复请求的稳定回放。
    const int index     = std::min(seq.cursor, lastIndex);
    out                 = seq.items.at(index);
    if (seq.cursor < lastIndex) {
        seq.cursor += 1;
    }
    return true;
}

void QCNetworkMockHandler::setCaptureEnabled(bool enabled)
{
    QMutexLocker locker(&d_ptr->mutex);
    d_ptr->captureEnabled = enabled;
}

bool QCNetworkMockHandler::captureEnabled() const
{
    QMutexLocker locker(&d_ptr->mutex);
    return d_ptr->captureEnabled;
}

void QCNetworkMockHandler::setCaptureBodyPreviewLimit(int bytes)
{
    QMutexLocker locker(&d_ptr->mutex);
    d_ptr->captureBodyPreviewLimitBytes = bytes < 0 ? 0 : bytes;
}

int QCNetworkMockHandler::captureBodyPreviewLimit() const
{
    QMutexLocker locker(&d_ptr->mutex);
    return d_ptr->captureBodyPreviewLimitBytes;
}

void QCNetworkMockHandler::recordRequest(const CapturedRequest &request)
{
    QMutexLocker locker(&d_ptr->mutex);
    if (!d_ptr->captureEnabled) {
        return;
    }
    d_ptr->capturedRequests.append(request);
}

QList<QCNetworkMockHandler::CapturedRequest> QCNetworkMockHandler::capturedRequests() const
{
    QMutexLocker locker(&d_ptr->mutex);
    return d_ptr->capturedRequests;
}

QList<QCNetworkMockHandler::CapturedRequest> QCNetworkMockHandler::takeCapturedRequests()
{
    QMutexLocker locker(&d_ptr->mutex);
    QList<CapturedRequest> out = d_ptr->capturedRequests;
    d_ptr->capturedRequests.clear();
    return out;
}

void QCNetworkMockHandler::clearCapturedRequests()
{
    QMutexLocker locker(&d_ptr->mutex);
    d_ptr->capturedRequests.clear();
}

void QCNetworkMockHandler::clear()
{
    QMutexLocker locker(&d_ptr->mutex);
    d_ptr->sequences.clear();
    d_ptr->capturedRequests.clear();
}

QString QCNetworkMockHandler::makeKey(HttpMethod method, const QUrl &url)
{
    return QString::number(static_cast<int>(method)) + QStringLiteral("|") + url.toString();
}

namespace {

void replaceSequence(QCNetworkMockSequence &seq, const Internal::QCNetworkMockData &item)
{
    seq.items.clear();
    seq.items.append(item);
    seq.cursor = 0;
}

void appendSequence(QCNetworkMockSequence &seq, const Internal::QCNetworkMockData &item)
{
    seq.items.append(item);
    // 注意：cursor 不回退；当 items 变长时，下次 consume 会继续从当前 cursor 开始。
    if (seq.cursor < 0) {
        seq.cursor = 0;
    }
}

} // namespace

bool Internal::QCNetworkMockHandlerAccess::consumeMock(QCNetworkMockHandler &handler,
                                                       HttpMethod method,
                                                       const QUrl &url,
                                                       QCNetworkMockData &out)
{
    return handler.consumeMock(method, url, out);
}

} // namespace QCurl
