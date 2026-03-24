/**
 * @file
 * @brief 声明 HTTP 方法枚举。
 */

#ifndef QCNETWORKHTTPMETHOD_H
#define QCNETWORKHTTPMETHOD_H

#include "QCGlobal.h"

namespace QCurl {

/**
 * @brief HTTP 请求方法
 */
enum class HttpMethod {
    Head,   ///< HEAD 请求（仅获取响应头）
    Get,    ///< GET 请求（获取完整响应）
    Post,   ///< POST 请求（发送数据）
    Put,    ///< PUT 请求（上传资源）
    Delete, ///< DELETE 请求（删除资源）
    Patch   ///< PATCH 请求（部分更新）
};

} // namespace QCurl

#endif // QCNETWORKHTTPMETHOD_H
