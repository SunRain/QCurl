#ifndef UTILITY_H
#define UTILITY_H

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




} //namespace QCurl

#endif // UTILITY_H
