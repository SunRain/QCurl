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


typedef int NetworkError;
const static NetworkError NetworkNoError = CURLE_OK;



} //namespace QCurl

#endif // QCUTILITY_H
