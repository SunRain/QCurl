#ifndef CURLGLOBALCONSTRUCTOR_H
#define CURLGLOBALCONSTRUCTOR_H

#include "QCGlobal.h"

#include <QObject>

namespace QCurl {

/**
 * @brief libcurl 全局初始化守卫
 *
 * 通过函数内静态实例在进程生命周期内调用 `curl_global_init()` / `curl_global_cleanup()`。
 */
class QCURL_EXPORT CurlGlobalConstructor : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief 析构并执行 curl_global_cleanup
     */
    ~CurlGlobalConstructor() override;

    /**
     * @brief 获取进程级守卫实例
     * @return 守卫指针（无需释放，也不要 delete）
     */
    static CurlGlobalConstructor *instance();

private:
    /**
     * @brief 构造并执行 curl_global_init
     * @param parent 父对象
     */
    explicit CurlGlobalConstructor(QObject *parent = nullptr);
};

} // namespace QCurl
#endif // CURLGLOBALCONSTRUCTOR_H
