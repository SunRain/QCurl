#include "QCCurlHandleManager.h"

#include "private/QCCurlOptionAdapter_p.h"
#include "private/QCNetworkReplyTransferState_p.h"

#include <QDebug>

#ifdef QCURL_ENABLE_TEST_HOOKS
#include "private/QCCurlRequiredOptionAdapter_p.h"

#include <atomic>
#endif
#include <utility>

namespace QCurl {
namespace {

#ifdef QCURL_ENABLE_TEST_HOOKS
std::atomic<int> s_easyCleanupCount{0};
#endif

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

QCCurlHandleManager::QCCurlHandleManager(std::nullptr_t) noexcept {}

QCCurlHandleManager::~QCCurlHandleManager()
{
    cleanup();
}

void QCCurlHandleManager::cleanup() noexcept
{
    // easy cleanup 仍可使用借用数据，因此先释放 easy，再释放 header list。
    if (m_curlHandle) {
        curl_easy_cleanup(std::exchange(m_curlHandle, nullptr));
#ifdef QCURL_ENABLE_TEST_HOOKS
        s_easyCleanupCount.fetch_add(1, std::memory_order_relaxed);
        Internal::recordShareCookieLifecycleForTest("easy-cleanup");
#endif
    }
    curl_slist_free_all(std::exchange(m_headerList, nullptr));
    m_transferState.clear();
}

void QCCurlHandleManager::retainTransferState(
    const QSharedPointer<QCNetworkReplyTransferState> &state) noexcept
{
    m_transferState = state;
}

#ifdef QCURL_ENABLE_TEST_HOOKS
int QCCurlHandleManager::cleanupCountForTest() noexcept
{
    return s_easyCleanupCount.load(std::memory_order_relaxed);
}
#endif

QCCurlHandleManager::QCCurlHandleManager(QCCurlHandleManager &&other) noexcept
    : m_runtimeLease(std::move(other.m_runtimeLease))
    , m_transferState(std::move(other.m_transferState))
    , m_curlHandle(std::exchange(other.m_curlHandle, nullptr))
    , m_headerList(std::exchange(other.m_headerList, nullptr))
    , m_initializationError(std::move(other.m_initializationError))
{}

QCCurlHandleManager &QCCurlHandleManager::operator=(QCCurlHandleManager &&other) noexcept
{
    if (this != &other) {
        cleanup();

        // 转移所有权
        m_runtimeLease        = std::move(other.m_runtimeLease);
        m_transferState       = std::move(other.m_transferState);
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
    if (Internal::CurlOptions::shouldForceSlistAppendFailure(
            QCURL_CURL_OPTION(CURLOPT_HTTPHEADER).name)) {
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
