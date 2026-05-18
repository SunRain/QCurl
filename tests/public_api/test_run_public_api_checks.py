from __future__ import annotations

from argparse import Namespace
from pathlib import Path
import subprocess
import sys

from tests.public_api import run_public_api_checks as public_api
from tests.public_api.consumer_contracts import validate_scheduler_core_contract_fixture
from tests.public_api import layout_scan


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

    rc = public_api.scan_hard_break_guards(Namespace(repo_root=tmp_path))

    assert rc == 1
    err = capsys.readouterr().err
    assert "old fromSingleFileDevice ownerThread" in err
    assert "releaseDevice" in err


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


def test_check_export_contract_rejects_bundled_libcurl_leak(tmp_path, capsys) -> None:
    stage = tmp_path / "stage"
    target_dir = stage / "lib" / "cmake" / "QCurl"
    target_dir.mkdir(parents=True)
    (target_dir / "QCurlTargets.cmake").write_text(
        "target_link_libraries(QCurl::QCurl INTERFACE QCurl::libcurl_shared)\n",
        encoding="utf-8",
    )

    rc = public_api.check_export_contract(Namespace(stage_dir=stage))

    assert rc == 1
    assert "QCurl::libcurl_shared" in capsys.readouterr().err


def test_check_blocking_extras_install_requires_opt_in_headers(tmp_path, capsys) -> None:
    default_include = tmp_path / "default" / "include" / "qcurl"
    blocking_include = tmp_path / "blocking" / "include" / "qcurl"
    default_include.mkdir(parents=True)
    blocking_include.mkdir(parents=True)
    (blocking_include / "QCBlockingNetworkClient.h").write_text("", encoding="utf-8")
    manifest = tmp_path / "blocking.txt"
    manifest.write_text("QCBlockingNetworkClient.h\n", encoding="utf-8")

    rc = public_api.check_blocking_extras_install(
        Namespace(
            default_stage_dir=tmp_path / "default",
            blocking_stage_dir=tmp_path / "blocking",
            manifest=manifest,
        )
    )

    assert rc == 0
    assert "blocking extras install contract passed" in capsys.readouterr().out


def test_check_blocking_extras_install_rejects_default_leak(tmp_path, capsys) -> None:
    default_include = tmp_path / "default" / "include" / "qcurl"
    blocking_include = tmp_path / "blocking" / "include" / "qcurl"
    default_include.mkdir(parents=True)
    blocking_include.mkdir(parents=True)
    (default_include / "QCBlockingNetworkClient.h").write_text("", encoding="utf-8")
    (blocking_include / "QCBlockingNetworkClient.h").write_text("", encoding="utf-8")
    manifest = tmp_path / "blocking.txt"
    manifest.write_text("QCBlockingNetworkClient.h\n", encoding="utf-8")

    rc = public_api.check_blocking_extras_install(
        Namespace(
            default_stage_dir=tmp_path / "default",
            blocking_stage_dir=tmp_path / "blocking",
            manifest=manifest,
        )
    )

    assert rc == 1
    assert "leaked into default Core stage" in capsys.readouterr().err


def test_check_test_support_install_requires_opt_in_headers(tmp_path, capsys) -> None:
    default_include = tmp_path / "default" / "include" / "qcurl"
    test_support_include = tmp_path / "test_support" / "include" / "qcurl"
    default_include.mkdir(parents=True)
    test_support_include.mkdir(parents=True)
    (test_support_include / "QCNetworkMockHandler.h").write_text("", encoding="utf-8")
    (test_support_include / "QCNetworkTestSupport.h").write_text("", encoding="utf-8")
    manifest = tmp_path / "test_support.txt"
    manifest.write_text("QCNetworkMockHandler.h\nQCNetworkTestSupport.h\n", encoding="utf-8")

    rc = public_api.check_test_support_install(
        Namespace(
            default_stage_dir=tmp_path / "default",
            test_support_stage_dir=tmp_path / "test_support",
            manifest=manifest,
        )
    )

    assert rc == 0
    assert "test support install contract passed" in capsys.readouterr().out


def test_check_test_support_install_rejects_default_leak(tmp_path, capsys) -> None:
    default_include = tmp_path / "default" / "include" / "qcurl"
    test_support_include = tmp_path / "test_support" / "include" / "qcurl"
    default_include.mkdir(parents=True)
    test_support_include.mkdir(parents=True)
    (default_include / "QCNetworkMockHandler.h").write_text("", encoding="utf-8")
    (test_support_include / "QCNetworkMockHandler.h").write_text("", encoding="utf-8")
    manifest = tmp_path / "test_support.txt"
    manifest.write_text("QCNetworkMockHandler.h\n", encoding="utf-8")

    rc = public_api.check_test_support_install(
        Namespace(
            default_stage_dir=tmp_path / "default",
            test_support_stage_dir=tmp_path / "test_support",
            manifest=manifest,
        )
    )

    assert rc == 1
    assert "leaked into default Core stage" in capsys.readouterr().err


def test_check_other_extras_install_requires_opt_in_headers(tmp_path, capsys) -> None:
    default_include = tmp_path / "default" / "include" / "qcurl"
    other_extras_include = tmp_path / "other_extras" / "include" / "qcurl"
    default_include.mkdir(parents=True)
    other_extras_include.mkdir(parents=True)
    (other_extras_include / "QCNetworkDiagnostics.h").write_text("", encoding="utf-8")
    manifest = tmp_path / "other_extras.txt"
    manifest.write_text("QCNetworkDiagnostics.h\n", encoding="utf-8")

    rc = public_api.check_other_extras_install(
        Namespace(
            default_stage_dir=tmp_path / "default",
            other_extras_stage_dir=tmp_path / "other_extras",
            manifest=manifest,
        )
    )

    assert rc == 0
    assert "other extras install contract passed" in capsys.readouterr().out


def test_check_other_extras_install_rejects_default_leak(tmp_path, capsys) -> None:
    default_include = tmp_path / "default" / "include" / "qcurl"
    other_extras_include = tmp_path / "other_extras" / "include" / "qcurl"
    default_include.mkdir(parents=True)
    other_extras_include.mkdir(parents=True)
    (default_include / "QCNetworkDiagnostics.h").write_text("", encoding="utf-8")
    (other_extras_include / "QCNetworkDiagnostics.h").write_text("", encoding="utf-8")
    manifest = tmp_path / "other_extras.txt"
    manifest.write_text("QCNetworkDiagnostics.h\n", encoding="utf-8")

    rc = public_api.check_other_extras_install(
        Namespace(
            default_stage_dir=tmp_path / "default",
            other_extras_stage_dir=tmp_path / "other_extras",
            manifest=manifest,
        )
    )

    assert rc == 1
    assert "leaked into default Core stage" in capsys.readouterr().err


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
