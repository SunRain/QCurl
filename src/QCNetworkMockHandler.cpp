// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkMockHandler.h"

namespace QCurl {

QCNetworkMockHandler::QCNetworkMockHandler() = default;
QCNetworkMockHandler::~QCNetworkMockHandler() = default;

void QCNetworkMockHandler::mockResponse(const QUrl &url, const QByteArray &response, int statusCode)
{
    MockData data;
    data.response = response;
    data.statusCode = statusCode;
    data.isError = false;
    m_mocks[url.toString()] = data;
}

void QCNetworkMockHandler::mockError(const QUrl &url, NetworkError error)
{
    MockData data;
    data.error = error;
    data.isError = true;
    m_mocks[url.toString()] = data;
}

void QCNetworkMockHandler::setGlobalDelay(int msecs)
{
    m_globalDelay = msecs;
}

int QCNetworkMockHandler::globalDelay() const
{
    return m_globalDelay;
}

bool QCNetworkMockHandler::hasMock(const QUrl &url) const
{
    return m_mocks.contains(url.toString());
}

QByteArray QCNetworkMockHandler::getMockResponse(const QUrl &url, int &statusCode) const
{
    if (m_mocks.contains(url.toString())) {
        const auto &data = m_mocks[url.toString()];
        statusCode = data.statusCode;
        return data.response;
    }
    statusCode = 0;
    return QByteArray();
}

NetworkError QCNetworkMockHandler::getMockError(const QUrl &url) const
{
    if (m_mocks.contains(url.toString())) {
        const auto &data = m_mocks[url.toString()];
        if (data.isError) {
            return data.error;
        }
    }
    return NetworkError::NoError;
}

bool QCNetworkMockHandler::isErrorMock(const QUrl &url) const
{
    if (m_mocks.contains(url.toString())) {
        return m_mocks[url.toString()].isError;
    }
    return false;
}

void QCNetworkMockHandler::clear()
{
    m_mocks.clear();
}

} // namespace QCurl
