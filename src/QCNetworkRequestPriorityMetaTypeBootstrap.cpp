// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkRequestPriority.h"

namespace {

void registerQCNetworkRequestPriorityMetaTypeOnLoad()
{
    // 让库在首个 queued connection / QSignalSpy 触发前就具备稳定的 canonical metatype。
    QCurl::registerQCNetworkRequestPriorityMetaType();
}

} // namespace

Q_CONSTRUCTOR_FUNCTION(registerQCNetworkRequestPriorityMetaTypeOnLoad)
