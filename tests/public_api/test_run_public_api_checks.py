from __future__ import annotations

from argparse import Namespace
from pathlib import Path
import subprocess
import sys

from tests.public_api import run_public_api_checks as public_api
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


def test_consumer_contract_fixture_requires_core_snippets(tmp_path) -> None:
    fixture_dir = tmp_path / "consumer"
    fixture_dir.mkdir()
    (fixture_dir / "main.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")

    try:
        public_api.validate_scheduler_core_contract_fixture(fixture_dir)
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
