from __future__ import annotations

from pathlib import Path

from scripts import check_libcurl_binding_contract as binding


def _write_handle_manager(root: Path, source: str) -> Path:
    path = root / "src" / "QCCurlHandleManager.cpp"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(source, encoding="utf-8")
    return root / "src"


def test_handle_manager_requires_runtime_lease_before_easy_creation(tmp_path: Path) -> None:
    source = """
#include <curl/curl.h>

namespace QCurl {
namespace {
CURL *createEasyHandle(const Internal::RuntimeLease &runtimeLease, QString *error)
{
    if (!runtimeLease.isValid()) {
        return nullptr;
    }
    CURL *handle = curl_easy_init();
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    return handle;
}
}
QCCurlHandleManager::QCCurlHandleManager()
    : m_runtimeLease(Internal::acquireRuntimeLease())
{
    m_curlHandle = createEasyHandle(m_runtimeLease, &m_initializationError);
}
QCCurlHandleManager::~QCCurlHandleManager() = default;
}
"""

    assert binding.check_handle_manager_order(_write_handle_manager(tmp_path, source)) == []


def test_handle_manager_rejects_easy_creation_without_runtime_admission(tmp_path: Path) -> None:
    source = """
CURL *createEasyHandle(QString *error)
{
    CURL *handle = curl_easy_init();
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    return handle;
}
QCCurlHandleManager::QCCurlHandleManager()
{
    m_curlHandle = createEasyHandle(&m_initializationError);
}
QCCurlHandleManager::~QCCurlHandleManager() = default;
"""

    violations = binding.check_handle_manager_order(_write_handle_manager(tmp_path, source))

    assert any("runtime lease parameter" in violation for violation in violations)
    assert any("missing acquireRuntimeLease" in violation for violation in violations)


def test_handle_manager_rejects_runtime_admission_after_helper_call(tmp_path: Path) -> None:
    source = """
CURL *createEasyHandle(const Internal::RuntimeLease &runtimeLease, QString *error)
{
    if (!runtimeLease.isValid()) {
        return nullptr;
    }
    CURL *handle = curl_easy_init();
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    return handle;
}
QCCurlHandleManager::QCCurlHandleManager()
{
    m_curlHandle = createEasyHandle(m_runtimeLease, &m_initializationError);
    m_runtimeLease = Internal::acquireRuntimeLease();
}
QCCurlHandleManager::~QCCurlHandleManager() = default;
"""

    violations = binding.check_handle_manager_order(_write_handle_manager(tmp_path, source))

    assert any("acquireRuntimeLease must precede handle creation" in violation for violation in violations)


def test_handle_manager_rejects_runtime_validation_after_easy_init(tmp_path: Path) -> None:
    source = """
CURL *createEasyHandle(const Internal::RuntimeLease &runtimeLease, QString *error)
{
    CURL *handle = curl_easy_init();
    if (!runtimeLease.isValid()) {
        return nullptr;
    }
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    return handle;
}
QCCurlHandleManager::QCCurlHandleManager()
    : m_runtimeLease(Internal::acquireRuntimeLease())
{
    m_curlHandle = createEasyHandle(m_runtimeLease, &m_initializationError);
}
QCCurlHandleManager::~QCCurlHandleManager() = default;
"""

    violations = binding.check_handle_manager_order(_write_handle_manager(tmp_path, source))

    assert any("runtimeLease.isValid must precede curl_easy_init" in violation for violation in violations)
