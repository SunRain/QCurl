// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#ifndef QCNETWORKREPLYPROPERTIES_P_H
#define QCNETWORKREPLYPROPERTIES_P_H

namespace QCurl::Internal::replyproperties {

// 跨对象协作的私有属性；文本和值类型不属于公共属性 API。
inline constexpr char kDestroying[]            = "_qcurl_reply_destroying";
inline constexpr char kResumableExistingSize[] = "_qcurl_resumable_existing_size";

} // namespace QCurl::Internal::replyproperties

#endif // QCNETWORKREPLYPROPERTIES_P_H
