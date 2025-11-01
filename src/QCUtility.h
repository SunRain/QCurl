#ifndef QCUTILITY_H
#define QCUTILITY_H

#include <curl/curl.h>

#include <QString>
#include <QUrl>

namespace QCurl {

template<typename T>
inline bool set(CURL *handle,  CURLoption option, T parameter) {
    return curl_easy_setopt(handle, option, parameter) == CURLE_OK;
}

inline bool set(CURL *handle, CURLoption option, const QString &parameter) {
    return set(handle, option, parameter.toUtf8().constData());
}

inline bool set(CURL *handle, CURLoption option, const QUrl &parameter) {
    return set(handle, option, parameter.toEncoded().constData());
}

// 旧的 typedef 已移除，请使用 QCNetworkError.h 中的 enum class NetworkError
// typedef int NetworkError;  // DEPRECATED - 使用 #include "QCNetworkError.h"
// const static NetworkError NetworkNoError = CURLE_OK;  // DEPRECATED - 使用 NetworkError::NoError

} //namespace QCurl

#endif // QCUTILITY_H
