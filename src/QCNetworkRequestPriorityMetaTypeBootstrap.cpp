// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkRequestPriority.h"

namespace {

void registerQCNetworkRequestPriorityMetaTypeOnLoad()
{
    QCurl::registerQCNetworkRequestPriorityMetaType();
}

} // namespace

Q_CONSTRUCTOR_FUNCTION(registerQCNetworkRequestPriorityMetaTypeOnLoad)
