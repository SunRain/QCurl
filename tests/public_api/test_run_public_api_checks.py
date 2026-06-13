from __future__ import annotations

from argparse import Namespace
from pathlib import Path
import subprocess
import sys

from tests.public_api import run_public_api_checks as public_api
from tests.public_api.consumer_contracts import validate_consumer_fixture
from tests.public_api.consumer_contracts import validate_scheduler_core_contract_fixture
from tests.public_api.consumer_cookie_contracts import validate_cookie_async_result_core_contract_fixture
from tests.public_api import layout_scan
from tests.public_api.blocking_extras_contracts import validate_blocking_extras_fixture

def test_strip_comments_and_strings_ignores_forbidden_tokens_in_text() -> None:
    source = """
// #include <curl/curl.h>
const char *s = "CURLcode curl_easy_perform";
class QCURL_EXPORT Good {
public:
    void run();
};
"""

    stripped = public_api.strip_comments_and_strings(source)

    assert "curl_easy_perform" not in stripped
    assert "QCURL_EXPORT Good" in stripped

def test_collect_exported_types_reads_qobject_inheritance() -> None:
    header = """
class QCURL_EXPORT Exported : public QObject {
public:
    ~Exported();
};
"""

    exported = layout_scan.collect_exported_types(public_api.strip_comments_and_strings(header))

    assert len(exported) == 1
    assert exported[0].name == "Exported"
    assert exported[0].inherits_qobject is True

def test_collect_exported_types_reads_other_extras_export_macro() -> None:
    header = """
class QCURL_OTHER_EXTRAS_EXPORT ExportedExtra : public QObject {
public:
    ~ExportedExtra();
};
"""

    exported = layout_scan.collect_exported_types(public_api.strip_comments_and_strings(header))

    assert len(exported) == 1
    assert exported[0].name == "ExportedExtra"
    assert exported[0].inherits_qobject is True

def test_collect_layout_findings_flags_public_fields_and_nested_structs() -> None:
    header = "QCNetworkExample.h"
    source = ""
    stripped = public_api.strip_comments_and_strings(
        """
struct QCURL_EXPORT Value {
    struct Nested { int n; };
    int publicField;
};
"""
    )

    findings = layout_scan.collect_layout_findings(header, stripped, source)
    keys = {item.key for item in findings}

    assert "public-fields:QCNetworkExample.h:Value" in keys
    assert "struct-layout:nested:QCNetworkExample.h:Value::Nested" in keys

def test_collect_layout_findings_flags_direct_private_layout_fields() -> None:
    header = "QCNetworkExample.h"
    source = ""
    stripped = public_api.strip_comments_and_strings(
        """
class QCURL_EXPORT Job : public QObject {
public:
    ~Job() override;

private:
    QPointer<QObject> manager;
    bool started = false;
};
"""
    )

    findings = layout_scan.collect_layout_findings(header, stripped, source)
    keys = {item.key for item in findings}

    assert "private-layout:direct-fields:QCNetworkExample.h:Job" in keys

def test_collect_layout_findings_treats_all_exported_classes_as_abi_sensitive() -> None:
    header = "QCBlockingNetworkClient.h"
    source = ""
    stripped = public_api.strip_comments_and_strings(
        """
class QCURL_EXPORT QCTransferProgress {
public:
    QCTransferProgress() = default;
    [[nodiscard]] qint64 bytesReceived() const noexcept;

private:
    qint64 m_bytesReceived = 0;
};
"""
    )

    findings = layout_scan.collect_layout_findings(header, stripped, source)
    keys = {item.key for item in findings}

    assert "private-layout:direct-fields:QCBlockingNetworkClient.h:QCTransferProgress" in keys

def test_collect_layout_findings_flags_websocket_pool_nested_structs() -> None:
    header = "QCWebSocketPool.h"
    source = ""
    stripped = public_api.strip_comments_and_strings(
        """
class QCURL_OTHER_EXTRAS_EXPORT QCWebSocketPool : public QObject {
public:
    struct Config { int maxPoolSize = 10; };
    struct Stats { int totalConnections = 0; };
    explicit QCWebSocketPool(const Config &config);
    Stats statistics() const;
};
"""
    )

    findings = layout_scan.collect_layout_findings(header, stripped, source)
    keys = {item.key for item in findings}

    assert "struct-layout:nested:QCWebSocketPool.h:QCWebSocketPool::Config" in keys
    assert "struct-layout:nested:QCWebSocketPool.h:QCWebSocketPool::Stats" in keys

def test_collect_layout_findings_allows_private_incomplete_holder() -> None:
    header = "QCNetworkExample.h"
    source = "Job::~Job() = default;"
    stripped = public_api.strip_comments_and_strings(
        """
class JobPrivate;
class QCURL_EXPORT Job : public QObject {
public:
    ~Job() override;

private:
    Q_DECLARE_PRIVATE(Job)
    QScopedPointer<JobPrivate> d_ptr;
};
"""
    )

    findings = layout_scan.collect_layout_findings(header, stripped, source)
    keys = {item.key for item in findings}

    assert "private-layout:direct-fields:QCNetworkExample.h:Job" not in keys

def test_scan_headers_fails_stale_allowlist(tmp_path, capsys) -> None:
    source_root = tmp_path / "src"
    source_root.mkdir()
    (source_root / "QCNetworkClean.h").write_text(
        "class QCURL_EXPORT Clean { public: void run(); };\n",
        encoding="utf-8",
    )
    manifest = tmp_path / "manifest.txt"
    manifest.write_text("QCNetworkClean.h\n", encoding="utf-8")
    allowlist = tmp_path / "allowlist.txt"
    allowlist.write_text("stale:key\n", encoding="utf-8")

    rc = public_api.scan_headers(
        Namespace(
            source_root=source_root,
            manifest=manifest,
            layout_allowlist=allowlist,
        )
    )

    assert rc == 1
    assert "layout allowlist has stale entries" in capsys.readouterr().err

def test_scan_headers_rejects_qtnetwork_cookie_in_core_header(tmp_path, capsys) -> None:
    source_root = tmp_path / "src"
    source_root.mkdir()
    (source_root / "QCCookieLeak.h").write_text(
        "#include <QNetworkCookie>\n"
        "class QCURL_EXPORT QCCookieLeak { QList<QNetworkCookie> cookies() const; };\n",
        encoding="utf-8",
    )
    manifest = tmp_path / "manifest.txt"
    manifest.write_text("QCCookieLeak.h\n", encoding="utf-8")
    allowlist = tmp_path / "allowlist.txt"
    allowlist.write_text("", encoding="utf-8")

    rc = public_api.scan_headers(
        Namespace(
            source_root=source_root,
            manifest=manifest,
            layout_allowlist=allowlist,
        )
    )

    assert rc == 1
    err = capsys.readouterr().err
    assert "QtNetwork cookie include" in err
    assert "QtNetwork cookie type leak" in err

def test_curl_option_adapter_uses_checked_connect_timeout_conversion() -> None:
    adapter = Path(__file__).resolve().parents[2] / "src" / "private" / "QCCurlOptionAdapter_p.h"
    source = adapter.read_text(encoding="utf-8")
    set_timeout = source.split("[[nodiscard]] inline CURLcode setConnectTimeout", 1)[1]

    assert "tryCurlMilliseconds" in source
    assert "tryCurlMilliseconds(timeout, &timeoutMs)" in set_timeout
    assert "static_cast<long>(timeout.count())" not in set_timeout

def test_curl_option_adapter_keeps_websocket_options_feature_guarded() -> None:
    adapter = Path(__file__).resolve().parents[2] / "src" / "private" / "QCCurlOptionAdapter_p.h"
    source = adapter.read_text(encoding="utf-8")

    websocket_header = "#include <curl/websockets.h>"
    header_index = source.index(websocket_header)
    header_guard_start = source.rfind("#ifdef QCURL_WEBSOCKET_SUPPORT", 0, header_index)
    header_guard_end = source.find("#endif", header_index)
    assert header_guard_start != -1
    assert header_guard_end != -1

    no_auto_pong_index = source.index("setWebSocketNoAutoPong")
    function_guard_start = source.rfind("#ifdef QCURL_WEBSOCKET_SUPPORT", 0, no_auto_pong_index)
    function_guard_end = source.find("#endif // QCURL_WEBSOCKET_SUPPORT", no_auto_pong_index)
    assert function_guard_start != -1
    assert function_guard_end != -1

def test_websocket_options_reuses_curl_timeout_range_guard() -> None:
    options_impl = Path(__file__).resolve().parents[2] / "src" / "QCWebSocketOptions.cpp"
    source = options_impl.read_text(encoding="utf-8")

    assert "Internal::CurlOptions::tryCurlMilliseconds(timeout, &timeoutMs)" in source
    assert "connectTimeout 超出 libcurl 毫秒超时可表达范围" in source

def test_hard_break_guards_reject_removed_api_shapes(tmp_path, capsys) -> None:
    src = tmp_path / "src"
    src.mkdir()
    (src / "QCNetworkMultipartBody.h").write_text(
        "static QCNetworkMultipartBody fromSingleFileDevice(QThread *ownerThread);\n"
        "QIODevice *releaseDevice();\n",
        encoding="utf-8",
    )
    (src / "QCNetworkRequest.h").write_text(
        "class QCNetworkRequest { public: virtual ~QCNetworkRequest(); };\n",
        encoding="utf-8",
    )
    (src / "QCBlockingNetworkClient.h").write_text(
        "using QCBlockingProgressCallback = std::function<bool()>;\n",
        encoding="utf-8",
    )
    (src / "QCNetworkReply.h").write_text(
        "enum class ExecutionMode { Async, Sync };\n"
        "class QCNetworkReply { public: void setWriteCallback(); };\n"
        "using DataFunction = int;\n"
        "using SeekFunction = int;\n"
        "using ProgressFunction = int;\n",
        encoding="utf-8",
    )
    (src / "QCNetworkAccessManager.h").write_text(
        "class QCNetworkAccessManager { public: void sendGet(); };\n",
        encoding="utf-8",
    )
    qcurl_tests = tmp_path / "tests" / "qcurl"
    qcurl_tests.mkdir(parents=True)
    (qcurl_tests / "tst_QCNetworkReply.cpp").write_text(
        "void f() { reply.setWriteCallback(); }\n",
        encoding="utf-8",
    )

    rc = public_api.scan_hard_break_guards(Namespace(repo_root=tmp_path))

    assert rc == 1
    err = capsys.readouterr().err
    assert "old fromSingleFileDevice ownerThread" in err
    assert "releaseDevice" in err
    assert "removed QCNetworkAccessManager sendGet" in err
    assert "QCNetworkRequest virtual destructor" in err
    assert "Blocking Extras std::function progress callback" in err
    assert "removed QCNetworkReply ExecutionMode" in err
    assert "removed QCNetworkReply DataFunction typedef" in err
    assert "removed QCNetworkReply SeekFunction typedef" in err
    assert "removed QCNetworkReply ProgressFunction typedef" in err
    assert "removed QCNetworkReply callback setter" in err
    assert "removed QCNetworkReply callback setter call" in err

def test_surface_manifest_accepts_default_core_and_opt_in_extras(tmp_path, capsys) -> None:
    surface = tmp_path / "surface.json"
    surface.write_text(
        """
{
  "schemaVersion": 1,
  "layers": ["Core", "Blocking Extras", "Other Extras", "Test Support", "Internal"],
  "headers": [
    {
      "path": "QCNetworkAccessManager.h",
      "layer": "Core",
      "currentInstall": "core-default",
      "targetInstall": "core-default"
    },
    {
      "path": "QCNetworkMockHandler.h",
      "layer": "Test Support",
      "currentInstall": "test-support",
      "targetInstall": "test-support",
      "extractionTask": "T8"
    },
    {
      "path": "QCNetworkDiagnostics.h",
      "layer": "Other Extras",
      "currentInstall": "conditional-extras",
      "targetInstall": "other-extras"
    }
  ],
  "plannedHeaders": [
    {
      "path": "QCBlockingNetworkClient.h",
      "layer": "Blocking Extras",
      "currentInstall": "none",
      "targetInstall": "blocking-extras",
      "extractionTask": "T2"
    }
  ],
  "symbolExtractions": []
}
""",
        encoding="utf-8",
    )
    core = tmp_path / "core.txt"
    core.write_text("QCNetworkAccessManager.h\n", encoding="utf-8")
    extras = tmp_path / "extras.txt"
    extras.write_text("QCNetworkMockHandler.h\nQCNetworkDiagnostics.h\n", encoding="utf-8")

    rc = public_api.validate_surface_manifest(
        Namespace(surface_manifest=surface, core_manifest=core, extras_manifest=extras),
        fail_func=public_api.fail,
    )

    assert rc == 0
    assert "surface manifest passed" in capsys.readouterr().out

def test_surface_manifest_accepts_installed_blocking_extras(tmp_path, capsys) -> None:
    surface = tmp_path / "surface.json"
    surface.write_text(
        """
{
  "schemaVersion": 1,
  "layers": ["Core", "Blocking Extras", "Other Extras", "Test Support", "Internal"],
  "headers": [
    {
      "path": "QCNetworkAccessManager.h",
      "layer": "Core",
      "currentInstall": "core-default",
      "targetInstall": "core-default"
    },
    {
      "path": "QCBlockingNetworkClient.h",
      "layer": "Blocking Extras",
      "currentInstall": "blocking-extras",
      "targetInstall": "blocking-extras"
    }
  ],
  "plannedHeaders": [],
  "symbolExtractions": []
}
""",
        encoding="utf-8",
    )
    core = tmp_path / "core.txt"
    core.write_text("QCNetworkAccessManager.h\n", encoding="utf-8")
    extras = tmp_path / "extras.txt"
    extras.write_text("QCBlockingNetworkClient.h\n", encoding="utf-8")

    rc = public_api.validate_surface_manifest(
        Namespace(surface_manifest=surface, core_manifest=core, extras_manifest=extras),
        fail_func=public_api.fail,
    )

    assert rc == 0
    assert "surface manifest passed" in capsys.readouterr().out

def test_surface_manifest_rejects_mismatched_test_support_install(tmp_path, capsys) -> None:
    surface = tmp_path / "surface.json"
    surface.write_text(
        """
{
  "schemaVersion": 1,
  "layers": ["Core", "Blocking Extras", "Other Extras", "Test Support", "Internal"],
  "headers": [
    {
      "path": "QCNetworkAccessManager.h",
      "layer": "Core",
      "currentInstall": "core-default",
      "targetInstall": "core-default"
    },
    {
      "path": "QCNetworkMockHandler.h",
      "layer": "Test Support",
      "currentInstall": "core-default",
      "targetInstall": "test-support",
      "extractionTask": "T8"
    },
    {
      "path": "QCBlockingNetworkClient.h",
      "layer": "Blocking Extras",
      "currentInstall": "blocking-extras",
      "targetInstall": "blocking-extras"
    }
  ],
  "plannedHeaders": [],
  "symbolExtractions": []
}
""",
        encoding="utf-8",
    )
    core = tmp_path / "core.txt"
    core.write_text("QCNetworkAccessManager.h\n", encoding="utf-8")
    extras = tmp_path / "extras.txt"
    extras.write_text("QCNetworkMockHandler.h\nQCBlockingNetworkClient.h\n", encoding="utf-8")

    rc = public_api.validate_surface_manifest(
        Namespace(surface_manifest=surface, core_manifest=core, extras_manifest=extras),
        fail_func=public_api.fail,
    )

    assert rc == 1
    assert "QCNetworkMockHandler.h: currentInstall must be test-support" in capsys.readouterr().err

def test_surface_manifest_rejects_untracked_core_header(tmp_path, capsys) -> None:
    surface = tmp_path / "surface.json"
    surface.write_text(
        """
{
  "schemaVersion": 1,
  "layers": ["Core", "Blocking Extras", "Other Extras", "Test Support", "Internal"],
  "headers": [
    {
      "path": "QCNetworkAccessManager.h",
      "layer": "Core",
      "currentInstall": "core-default",
      "targetInstall": "core-default"
    }
  ],
  "plannedHeaders": [
    {
      "path": "QCBlockingNetworkClient.h",
      "layer": "Blocking Extras",
      "currentInstall": "none",
      "targetInstall": "blocking-extras",
      "extractionTask": "T2"
    }
  ],
  "symbolExtractions": []
}
""",
        encoding="utf-8",
    )
    core = tmp_path / "core.txt"
    core.write_text("QCNetworkAccessManager.h\nQCNetworkMockHandler.h\n", encoding="utf-8")
    extras = tmp_path / "extras.txt"
    extras.write_text("", encoding="utf-8")

    rc = public_api.validate_surface_manifest(
        Namespace(surface_manifest=surface, core_manifest=core, extras_manifest=extras),
        fail_func=public_api.fail,
    )

    assert rc == 1
    assert "QCNetworkMockHandler.h: missing from surface manifest" in capsys.readouterr().err

def test_consumer_contract_fixture_requires_core_snippets(tmp_path) -> None:
    fixture_dir = tmp_path / "consumer"
    fixture_dir.mkdir()
    (fixture_dir / "main.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")

    try:
        validate_scheduler_core_contract_fixture(fixture_dir)
    except RuntimeError as exc:
        assert "#include <QCNetworkSchedulerPolicy.h>" in str(exc)
    else:
        raise AssertionError("validate_scheduler_core_contract_fixture should fail")

def test_cookie_async_result_fixture_requires_core_snippets(tmp_path) -> None:
    fixture_dir = tmp_path / "consumer"
    fixture_dir.mkdir()
    (fixture_dir / "main.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")

    try:
        validate_cookie_async_result_core_contract_fixture(fixture_dir)
    except RuntimeError as exc:
        assert "#include <QCCookieAsyncResult.h>" in str(exc)
    else:
        raise AssertionError("validate_cookie_async_result_core_contract_fixture should fail")

def test_cookie_async_result_fixture_rejects_legacy_qnetworkcookie_contract(tmp_path) -> None:
    fixture_dir = tmp_path / "legacy_consumer"
    fixture_dir.mkdir()
    (fixture_dir / "main.cpp").write_text(
        """
#include <QCCookieAsyncResult.h>

#include <QList>
#include <QNetworkCookie>

int main()
{
    QList<QNetworkCookie> cookies;
    const auto result = QCurl::QCCookieExportResult::success(cookies);
    return result.cookies().isEmpty() ? 0 : 1;
}
""",
        encoding="utf-8",
    )

    try:
        validate_cookie_async_result_core_contract_fixture(fixture_dir)
    except RuntimeError as exc:
        assert "must not use QNetworkCookie" in str(exc)
    else:
        raise AssertionError("legacy QNetworkCookie consumer fixture should fail")

def test_cookie_async_result_fixture_rejects_qtnetwork_default_consumer(tmp_path) -> None:
    fixture_dir = tmp_path / "consumer"
    fixture_dir.mkdir()
    (fixture_dir / "main.cpp").write_text(
        """
#include <QCCookie.h>
#include <QCCookieAsyncResult.h>

int main()
{
    QCurl::QCCookie exportedCookie;
    exportedCookie.setName(QByteArrayLiteral("session"));
    exportedCookie.setValue(QByteArrayLiteral("value"));
    const auto cookieImportSuccess = QCurl::QCCookieOperationResult::success();
    const auto cookieImportFailure = QCurl::QCCookieOperationResult::failure(QStringLiteral("err"));
    const auto cookieExportSuccess =
        QCurl::QCCookieExportResult::success({exportedCookie});
    const auto cookieExportFailure = QCurl::QCCookieExportResult::failure(QStringLiteral("err"));
    return cookieImportSuccess.isSuccess() && !cookieImportFailure.error().isEmpty()
        && !cookieExportSuccess.cookies().isEmpty() && !cookieExportFailure.error().isEmpty()
        ? 0
        : 1;
}
""",
        encoding="utf-8",
    )
    (fixture_dir / "CMakeLists.txt").write_text(
        "find_package(Qt6 REQUIRED COMPONENTS Core Network)\n"
        "target_link_libraries(app PRIVATE Qt6::Network)\n",
        encoding="utf-8",
    )

    try:
        validate_cookie_async_result_core_contract_fixture(fixture_dir)
    except RuntimeError as exc:
        assert "must not require QtNetwork" in str(exc)
    else:
        raise AssertionError("QtNetwork default consumer fixture should fail")

def test_consumer_smoke_fixture_matches_current_core_contracts() -> None:
    fixture_dir = Path(__file__).resolve().parent / "consumer_smoke"

    validate_consumer_fixture(fixture_dir)

def test_blocking_extras_fixture_requires_bounded_body_and_download_contracts(tmp_path) -> None:
    fixture_dir = tmp_path / "blocking"
    fixture_dir.mkdir()
    (fixture_dir / "main.cpp").write_text("#include <QCBlockingNetworkClient.h>\n", encoding="utf-8")

    try:
        validate_blocking_extras_fixture(fixture_dir)
    except RuntimeError as exc:
        message = str(exc)
        assert "QCurl::QCBlockingRequestOptions requestOptions" in message
        assert "client.downloadToDevice(request, &output, requestOptions)" in message
    else:
        raise AssertionError("validate_blocking_extras_fixture should fail")

def test_public_api_script_help_imports_layout_scan_from_repo_root() -> None:
    script = Path(public_api.__file__)

    proc = subprocess.run(
        [sys.executable, str(script), "--help"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )

    assert proc.returncode == 0
    assert "QCurl public API guardrail checks" in proc.stdout

def test_pkg_config_contract_rejects_core_zlib(tmp_path, capsys) -> None:
    stage = tmp_path / "stage"
    pc_dir = stage / "lib" / "pkgconfig"
    pc_dir.mkdir(parents=True)
    (pc_dir / "qcurl.pc").write_text(
        "Requires: Qt6Core >= 6.2\n"
        "Requires.private: libcurl >= 7.85.0\n"
        "Libs: -L${libdir} -lQCurl\n"
        "Libs.private: -lz\n",
        encoding="utf-8",
    )
    (pc_dir / "qcurl-other-extras.pc").write_text(
        "Requires: qcurl = 1.0.0, Qt6Network >= 6.2\n"
        "Requires.private: zlib\n"
        "Libs: -L${libdir} -lQCurlOtherExtras\n",
        encoding="utf-8",
    )

    rc = public_api.check_pkg_config_contract(
        Namespace(stage_dir=stage, pkg_config="pkg-config")
    )

    assert rc == 1
    assert "must not carry zlib" in capsys.readouterr().err

def test_pkg_config_contract_rejects_core_qtnetwork(tmp_path, capsys) -> None:
    stage = tmp_path / "stage"
    pc_dir = stage / "lib" / "pkgconfig"
    pc_dir.mkdir(parents=True)
    (pc_dir / "qcurl.pc").write_text(
        "Requires: Qt6Core >= 6.2, Qt6Network >= 6.2\n"
        "Requires.private: libcurl >= 7.85.0\n"
        "Libs: -L${libdir} -lQCurl\n",
        encoding="utf-8",
    )
    (pc_dir / "qcurl-other-extras.pc").write_text(
        "Requires: qcurl = 1.0.0, Qt6Network >= 6.2\n"
        "Requires.private: zlib\n"
        "Libs: -L${libdir} -lQCurlOtherExtras\n",
        encoding="utf-8",
    )

    rc = public_api.check_pkg_config_contract(
        Namespace(stage_dir=stage, pkg_config="pkg-config")
    )

    assert rc == 1
    assert "must not carry Qt6Network" in capsys.readouterr().err

def test_pkg_config_contract_requires_other_extras_pc(tmp_path, capsys) -> None:
    stage = tmp_path / "stage"
    pc_dir = stage / "lib" / "pkgconfig"
    pc_dir.mkdir(parents=True)
    (pc_dir / "qcurl.pc").write_text(
        "Requires: Qt6Core >= 6.2\n"
        "Requires.private: libcurl >= 7.85.0\n"
        "Libs: -L${libdir} -lQCurl\n",
        encoding="utf-8",
    )

    rc = public_api.check_pkg_config_contract(
        Namespace(stage_dir=stage, pkg_config="pkg-config")
    )

    assert rc == 1
    assert "qcurl-other-extras.pc not found" in capsys.readouterr().err


def test_release_metadata_scan_rejects_legacy_identity(tmp_path, capsys) -> None:
    import scripts.run_release_gate as release_gate

    (tmp_path / "README.md").write_text("QCurl 3.0.0 current release\n", encoding="utf-8")
    (tmp_path / "SYSTEM_DOCUMENTATION.md").write_text("QCurl 1.0.0\n", encoding="utf-8")
    (tmp_path / "CMakeLists.txt").write_text("project(QCurl VERSION 1.0.0)\n", encoding="utf-8")

    assert release_gate._scan_metadata(tmp_path) == 1
    assert "forbidden legacy release identity" in capsys.readouterr().err


def test_release_metadata_scan_allows_external_protocol_versions(tmp_path, capsys) -> None:
    import scripts.run_release_gate as release_gate

    (tmp_path / "README.md").write_text("HTTP/3 and Qt 6 are external versions\n", encoding="utf-8")
    (tmp_path / "SYSTEM_DOCUMENTATION.md").write_text("libcurl supports HTTP/2 and HTTP/3\n", encoding="utf-8")
    (tmp_path / "CMakeLists.txt").write_text("project(QCurl VERSION 1.0.0)\n", encoding="utf-8")

    assert release_gate._scan_metadata(tmp_path) == 0
    assert "metadata scan passed" in capsys.readouterr().out
