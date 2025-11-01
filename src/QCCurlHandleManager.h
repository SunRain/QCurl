#ifndef QCCURLHANDLEMANAGER_H
#define QCCURLHANDLEMANAGER_H

#include <curl/curl.h>
#include <QString>

QT_BEGIN_NAMESPACE

namespace QCurl {

/**
 * @brief RAII 风格的 curl 句柄管理器
 *
 * 自动管理 CURL 句柄和 curl_slist 的生命周期，确保资源正确释放。
 * 使用 RAII 模式消除内存泄漏风险。
 *
 * @par 特性
 * - 构造时自动初始化 curl easy handle
 * - 析构时自动清理 curl easy handle 和 header list
 * - 支持移动语义，禁止拷贝
 * - 线程安全（每个实例独立使用）
 *
 * @par 示例
 * @code
 * QCCurlHandleManager manager;
 * if (manager.handle()) {
 *     manager.appendHeader("Content-Type: application/json");
 *     curl_easy_setopt(manager.handle(), CURLOPT_URL, "https://example.com");
 *     curl_easy_perform(manager.handle());
 * }
 * // 析构时自动清理
 * @endcode
 *
 */
class QCCurlHandleManager
{
public:
    /**
     * @brief 构造函数，初始化 curl easy handle
     *
     * 调用 curl_easy_init() 创建句柄。如果失败，handle() 返回 nullptr。
     */
    QCCurlHandleManager();

    /**
     * @brief 析构函数，自动清理资源
     *
     * 调用 curl_easy_cleanup() 和 curl_slist_free_all()。
     */
    ~QCCurlHandleManager();

    // 移动构造函数
    QCCurlHandleManager(QCCurlHandleManager &&other) noexcept;

    // 移动赋值运算符
    QCCurlHandleManager& operator=(QCCurlHandleManager &&other) noexcept;

    // 禁止拷贝
    QCCurlHandleManager(const QCCurlHandleManager&) = delete;
    QCCurlHandleManager& operator=(const QCCurlHandleManager&) = delete;

    /**
     * @brief 获取 curl easy handle 指针
     *
     * @return CURL* curl 句柄指针，如果初始化失败则返回 nullptr
     */
    [[nodiscard]] CURL* handle() const noexcept { return m_curlHandle; }

    /**
     * @brief 获取 HTTP header 列表
     *
     * @return curl_slist* header 列表指针，初始为 nullptr
     */
    [[nodiscard]] curl_slist* headerList() const noexcept { return m_headerList; }

    /**
     * @brief 添加 HTTP header
     *
     * @param header header 字符串，格式为 "Key: Value"
     *
     * @par 示例
     * @code
     * manager.appendHeader("Content-Type: application/json");
     * manager.appendHeader("Authorization: Bearer token123");
     * @endcode
     */
    void appendHeader(const QString &header);

    /**
     * @brief 检查句柄是否有效
     *
     * @return bool true 表示句柄有效，false 表示初始化失败
     */
    [[nodiscard]] bool isValid() const noexcept { return m_curlHandle != nullptr; }

private:
    CURL *m_curlHandle = nullptr;       ///< curl easy handle
    curl_slist *m_headerList = nullptr; ///< HTTP header 列表
};

} // namespace QCurl
QT_END_NAMESPACE

#endif // QCCURLHANDLEMANAGER_H
