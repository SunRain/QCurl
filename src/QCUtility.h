/**
 * @file
 * @brief 声明通用网络辅助函数。
 */

#ifndef QCUTILITY_H
#define QCUTILITY_H

#include <curl/curl.h>

/**
 * @brief internal curl_easy_setopt 辅助函数
 *
 * 提供最薄的一层 `curl_easy_setopt` 返回值布尔化包装。
 */
namespace QCurl {

template<typename T>
inline bool set(CURL *handle, CURLoption option, T parameter)
{
    return curl_easy_setopt(handle, option, parameter) == CURLE_OK;
}

} // namespace QCurl

#endif // QCUTILITY_H
