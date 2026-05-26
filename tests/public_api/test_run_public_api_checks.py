from __future__ import annotations

from argparse import Namespace
from pathlib import Path
import subprocess
import sys

from tests.public_api import run_public_api_checks as public_api
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
        assert "#include <QCNetworkRequestScheduler.h>" in str(exc)
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
        "Requires: Qt6Core >= 6.2, Qt6Network >= 6.2\n"
        "Requires.private: libcurl >= 7.85.0\n"
        "Libs: -L${libdir} -lQCurl\n"
        "Libs.private: -lz\n",
        encoding="utf-8",
    )
    (pc_dir / "qcurl-other-extras.pc").write_text(
        "Requires: qcurl = 1.0.0\n"
        "Requires.private: zlib\n"
        "Libs: -L${libdir} -lQCurlOtherExtras\n",
        encoding="utf-8",
    )

    rc = public_api.check_pkg_config_contract(
        Namespace(stage_dir=stage, pkg_config="pkg-config")
    )

    assert rc == 1
    assert "must not carry zlib" in capsys.readouterr().err

def test_pkg_config_contract_requires_other_extras_pc(tmp_path, capsys) -> None:
    stage = tmp_path / "stage"
    pc_dir = stage / "lib" / "pkgconfig"
    pc_dir.mkdir(parents=True)
    (pc_dir / "qcurl.pc").write_text(
        "Requires: Qt6Core >= 6.2, Qt6Network >= 6.2\n"
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
