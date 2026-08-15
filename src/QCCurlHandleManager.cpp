#include "QCCurlHandleManager.h"

#include "private/QCCurlOptionAdapter_p.h"

#include <QDebug>

#include <utility> // for std::exchange

namespace QCurl {
namespace {

CURL *createEasyHandle(const Internal::RuntimeLease &runtimeLease, QString *error)
{
    if (!runtimeLease.isValid()) {
        *error = runtimeLease.diagnostic();
        return nullptr;
    }

    CURL *handle = curl_easy_init();
    if (!handle) {
        *error = QStringLiteral("curl_easy_init failed");
        return nullptr;
    }

    const CURLcode rc = Internal::CurlOptions::setEnabled(handle, CURLOPT_NOSIGNAL, true);
    if (rc != CURLE_OK) {
        qWarning() << "QCCurlHandleManager: failed to apply CURLOPT_NOSIGNAL:"
                   << curl_easy_strerror(rc);
        *error = QStringLiteral("curl_easy_init option setup failed: %1")
                     .arg(QString::fromUtf8(curl_easy_strerror(rc)));
        curl_easy_cleanup(handle);
        return nullptr;
    }

    return handle;
}

} // namespace

QCCurlHandleManager::QCCurlHandleManager()
    : m_runtimeLease(Internal::acquireRuntimeLease())
    , m_curlHandle(nullptr)
    , m_headerList(nullptr)
{
    m_curlHandle = createEasyHandle(m_runtimeLease, &m_initializationError);
    if (!m_curlHandle) {
        m_runtimeLease.reset();
    }
    // curl_easy_init() 可能返回 nullptr（内存不足等情况）
    // 调用者应该通过 isValid() 或检查 handle() != nullptr 来验证
}

QCCurlHandleManager::~QCCurlHandleManager()
{
    // 清理 header 列表
    if (m_headerList) {
        curl_slist_free_all(m_headerList);
        m_headerList = nullptr;
    }

    // 清理 curl 句柄
    if (m_curlHandle) {
        curl_easy_cleanup(m_curlHandle);
        m_curlHandle = nullptr;
    }
}

QCCurlHandleManager::QCCurlHandleManager(QCCurlHandleManager &&other) noexcept
    : m_runtimeLease(std::move(other.m_runtimeLease))
    , m_curlHandle(std::exchange(other.m_curlHandle, nullptr))
    , m_headerList(std::exchange(other.m_headerList, nullptr))
    , m_initializationError(std::move(other.m_initializationError))
{}

QCCurlHandleManager &QCCurlHandleManager::operator=(QCCurlHandleManager &&other) noexcept
{
    if (this != &other) {
        // 清理当前资源
        if (m_headerList) {
            curl_slist_free_all(m_headerList);
        }
        if (m_curlHandle) {
            curl_easy_cleanup(m_curlHandle);
        }

        // 转移所有权
        m_runtimeLease        = std::move(other.m_runtimeLease);
        m_curlHandle          = std::exchange(other.m_curlHandle, nullptr);
        m_headerList          = std::exchange(other.m_headerList, nullptr);
        m_initializationError = std::move(other.m_initializationError);
    }
    return *this;
}

bool QCCurlHandleManager::appendHeader(const QString &header)
{
    if (header.isEmpty()) {
        return true;
    }

    // curl_slist_append 会复制字符串，所以临时的 QByteArray 生命周期没问题
    QByteArray headerBytes = header.toUtf8();
    if (Internal::CurlOptions::shouldForceSlistAppendFailure("CURLOPT_HTTPHEADER")) {
        qWarning() << "QCCurlHandleManager::appendHeader: forced curl_slist_append failure";
        return false;
    }
    curl_slist *newHeaderList = curl_slist_append(m_headerList, headerBytes.constData());
    if (!newHeaderList) {
        qWarning() << "QCCurlHandleManager::appendHeader: curl_slist_append failed";
        return false;
    }

    m_headerList = newHeaderList;
    return true;
}

} // namespace QCurl
