#ifndef QCUTILITY_H
#define QCUTILITY_H

#include <QString>
#include <QUrl>

#include <curl/curl.h>

/**
 * @brief curl_easy_setopt 辅助函数
 *
 * 提供 Qt 类型到 libcurl 参数的便捷适配。
 */
namespace QCurl {

template<typename T>
inline bool set(CURL *handle, CURLoption option, T parameter)
{
    return curl_easy_setopt(handle, option, parameter) == CURLE_OK;
}

inline bool set(CURL *handle, CURLoption option, const QString &parameter)
{
    return set(handle, option, parameter.toUtf8().constData());
}

inline bool set(CURL *handle, CURLoption option, const QUrl &parameter)
{
    return set(handle, option, parameter.toEncoded().constData());
}

// 已移除旧 typedef
// 请用 QCNetworkError.h
// 使用 NetworkError 枚举

} // namespace QCurl

#endif // QCUTILITY_H
