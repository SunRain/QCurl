// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCGlobal.h"

#include "QCCookie.h"
#include "QCCookieAsyncResult.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkLaneKey.h"
#include "QCNetworkRequestPriority.h"
#include "QCNetworkSchedulerPolicy.h"

namespace QCurl {

void initialize()
{
    qRegisterMetaType<QCCookie>("QCurl::QCCookie");
    qRegisterMetaType<QCCookieOperationResult>("QCurl::QCCookieOperationResult");
    qRegisterMetaType<QCCookieExportResult>("QCurl::QCCookieExportResult");
    qRegisterMetaType<QCNetworkLaneKey>("QCurl::QCNetworkLaneKey");
    qRegisterMetaType<QCNetworkSchedulerPolicy>("QCurl::QCNetworkSchedulerPolicy");
    qRegisterMetaType<QCNetworkSchedulerPolicy::LaneConfig>(
        "QCurl::QCNetworkSchedulerPolicy::LaneConfig");
    qRegisterMetaType<QCNetworkSchedulerStatistics>("QCurl::QCNetworkSchedulerStatistics");
    qRegisterMetaType<QCNetworkLaneCancelResult>("QCurl::QCNetworkLaneCancelResult");
    registerQCNetworkRequestPriorityMetaType();
}

} // namespace QCurl
