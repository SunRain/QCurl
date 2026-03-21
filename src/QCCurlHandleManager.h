#ifndef QCCURLHANDLEMANAGER_H
#define QCCURLHANDLEMANAGER_H

#include <QString>

#include <curl/curl.h>

namespace QCurl {

/**
 * @brief internal easy handle / header list 生命周期守卫
 *
 * 负责管理 `CURL*` 与附属 `curl_slist*` 的释放，供库内实现复用。
 */
class QCCurlHandleManager
{
public:
    /**
     * @brief 初始化 easy handle
     *
     * 若 `curl_easy_init()` 失败，`handle()` 返回 `nullptr`。
     */
    QCCurlHandleManager();

    /**
     * @brief 释放 easy handle 与 header list
     */
    ~QCCurlHandleManager();

    // 移动构造函数
    QCCurlHandleManager(QCCurlHandleManager &&other) noexcept;

    // 移动赋值运算符
    QCCurlHandleManager &operator=(QCCurlHandleManager &&other) noexcept;

    // 禁止拷贝
    QCCurlHandleManager(const QCCurlHandleManager &)            = delete;
    QCCurlHandleManager &operator=(const QCCurlHandleManager &) = delete;

    /**
     * @brief 获取 curl easy handle 指针
     *
     * @return `CURL*`；初始化失败时返回 `nullptr`
     */
    [[nodiscard]] CURL *handle() const noexcept { return m_curlHandle; }

    /**
     * @brief 获取 HTTP header 列表
     *
     * @return 当前 header list；未添加任何 header 时为 `nullptr`
     */
    [[nodiscard]] curl_slist *headerList() const noexcept { return m_headerList; }

    /**
     * @brief 添加 HTTP header
     *
     * @param header 形如 `"Key: Value"` 的 header 行
     *
     * 失败时保留已有 header list，不会覆盖为 `nullptr`。
     */
    void appendHeader(const QString &header);

    /**
     * @brief 检查句柄是否有效
     *
     * @return bool true 表示句柄有效，false 表示初始化失败
     */
    [[nodiscard]] bool isValid() const noexcept { return m_curlHandle != nullptr; }

private:
    CURL *m_curlHandle       = nullptr; ///< curl easy handle
    curl_slist *m_headerList = nullptr; ///< HTTP header 列表
};

} // namespace QCurl

#endif // QCCURLHANDLEMANAGER_H
