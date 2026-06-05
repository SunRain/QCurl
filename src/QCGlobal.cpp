// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCGlobal.h"

#include "QCCookie.h"
#include "QCCookieAsyncResult.h"
#include "QCNetworkRequestPriority.h"

namespace QCurl {

void initialize()
{
    qRegisterMetaType<QCCookie>("QCurl::QCCookie");
    qRegisterMetaType<QCCookieOperationResult>("QCurl::QCCookieOperationResult");
    qRegisterMetaType<QCCookieExportResult>("QCurl::QCCookieExportResult");
    registerQCNetworkRequestPriorityMetaType();
}

} // namespace QCurl
