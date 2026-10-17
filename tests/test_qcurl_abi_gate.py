from __future__ import annotations

import os
import subprocess
from pathlib import Path

import pytest

from scripts import qcurl_abi_gate
from scripts import qcurl_abi_compare
from scripts import qcurl_abi_symbols


def _project_version() -> str:
    root = Path(__file__).resolve().parents[1]
    return ".".join(str(part) for part in qcurl_abi_gate.parse_project_version(root))


def _producer_library(filename: str) -> Path:
    root = Path(__file__).resolve().parents[1]
    build = Path(
        os.environ.get("QCURL_ABI_TEST_BUILD_DIR", root / "build-test-shared-gcc")
    )
    return build / "src" / filename


def test_shared_targets_use_target_wide_hidden_visibility() -> None:
    source = (Path(__file__).resolve().parents[1] / "src" / "CMakeLists.txt").read_text(
        encoding="utf-8"
    )

    assert "foreach(_qcurl_shared_target IN ITEMS QCurl QCurlOtherExtras)" in source
    assert "CXX_VISIBILITY_PRESET hidden" in source
    assert "VISIBILITY_INLINES_HIDDEN YES" in source
    assert "set_source_files_properties(" not in source


def test_static_export_macros_do_not_depend_on_visibility() -> None:
    source = (Path(__file__).resolve().parents[1] / "src" / "QCGlobal.h").read_text(
        encoding="utf-8"
    )

    assert "#if defined(QCURL_STATIC_DEFINE)\n#define QCURL_EXPORT" in source
    assert (
        "#if defined(QCURL_OTHER_EXTRAS_STATIC_DEFINE)\n"
        "#define QCURL_OTHER_EXTRAS_EXPORT" in source
    )


def test_core_allowlist_only_keeps_remaining_cross_library_bridges() -> None:
    assert qcurl_abi_symbols.INTERNAL_COMPONENT_BRIDGE_OWNERS == {
        "OtherExtras": frozenset(
            {"registerPersistentTransfer", "removePersistentTransfer"}
        ),
        "TestSupport": frozenset({"installNetworkMockProvider"}),
    }


def test_dynamic_symbol_allowlist_rejects_unlisted_first_party_owners() -> None:
    symbols = (
        "_ZN5QCurl18QCNetworkRequestC1Ev|QCurl::QCNetworkRequest::QCNetworkRequest()\n"
        "_ZN5QCurl14jitterFractionEv|QCurl::jitterFraction()\n"
        "_ZN5QCurl21QCNetworkReplyPrivate14loggerSnapshotEPKNS_14QCNetworkReplyE|"
        "QCurl::QCNetworkReplyPrivate::loggerSnapshot(QCurl::QCNetworkReply const*)\n"
        "_ZN5QCurl25QCCurlMultiTransferRecord6handleEv|"
        "QCurl::QCCurlMultiTransferRecord::handle() const\n"
        "_ZN5QCurl18QCCurlMultiManager11addTransferEONS_19QCCurlHandleManagerE|"
        "QCurl::QCCurlMultiManager::addTransfer(QCurl::QCCurlHandleManager&&)\n"
    )

    with pytest.raises(qcurl_abi_gate.AbiGateError) as exc_info:
        qcurl_abi_gate.validate_dynamic_symbol_contract(
            symbols,
            allowed_owners={"QCNetworkRequest"},
        )

    message = str(exc_info.value)
    assert "jitterFraction" in message
    assert "QCNetworkReplyPrivate" in message
    assert "QCCurlMultiTransferRecord" in message
    assert "QCCurlHandleManager" in message


def test_dynamic_symbol_allowlist_accepts_public_owner_and_non_first_party_symbols() -> (
    None
):
    symbols = (
        "_ZN5QCurl18QCNetworkRequestC1Ev|QCurl::QCNetworkRequest::QCNetworkRequest()\n"
        "_ZN10QByteArrayD1Ev|QByteArray::~QByteArray()\n"
    )

    qcurl_abi_gate.validate_dynamic_symbol_contract(
        symbols,
        allowed_owners={"QCNetworkRequest"},
    )


def test_snapshot_default_has_no_version_identity() -> None:
    args = qcurl_abi_gate.build_parser().parse_args(["snapshot"])
    qcurl_abi_gate._set_output_defaults(args)

    assert args.output == Path("build/abi/core.snapshot.xml")
    assert not qcurl_abi_gate.path_is_controlled_baseline(args.output)


def test_snapshot_command_rejects_controlled_baseline_output(tmp_path: Path) -> None:
    repo = tmp_path / "repo"
    controlled = repo / "abi" / "baseline" / "qcurl-core-v1.abi.xml"

    with pytest.raises(qcurl_abi_gate.AbiGateError, match="historical"):
        qcurl_abi_gate.validate_snapshot_output(controlled, repo_root=repo)


def test_hardbreak_report_is_nonempty_when_abidiff_reports_no_change(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(
        qcurl_abi_compare.subprocess,
        "run",
        lambda *_args, **_kwargs: subprocess.CompletedProcess([], 0, "", ""),
    )
    report = tmp_path / "old-to-new.txt"

    qcurl_abi_compare.run_abidiff_report(["abidiff"], report)

    assert report.read_text(encoding="utf-8").strip() == (
        "No ABI changes detected (abidiff returncode=0)."
    )


def test_core_dynamic_symbols_exclude_multi_manager_owner() -> None:
    library = _producer_library(f"libQCurl.so.{_project_version()}")

    symbols = qcurl_abi_gate._collect_dynamic_symbols(library)
    leaked_symbols = [
        line for line in symbols.splitlines() if "QCCurlMultiManager" in line
    ]

    assert not leaked_symbols, "Core 动态 ABI 泄漏 QCCurlMultiManager：\n" + "\n".join(
        leaked_symbols
    )


def test_other_extras_opaque_bridge_consumer() -> None:
    library = _producer_library(f"libQCurlOtherExtras.so.{_project_version()}")

    symbols = subprocess.run(
        ["nm", "-D", "--undefined-only", "--format=posix", str(library)],
        check=False,
        capture_output=True,
        text=True,
    )
    assert symbols.returncode == 0, symbols.stderr

    mangled_names = [
        line.split(None, 1)[0] for line in symbols.stdout.splitlines() if line.strip()
    ]
    demangled = subprocess.run(
        ["c++filt"],
        input="\n".join(mangled_names) + "\n",
        check=False,
        capture_output=True,
        text=True,
    )
    assert demangled.returncode == 0, demangled.stderr

    bridge_symbols = demangled.stdout.splitlines()
    assert any("QCurl::registerPersistentTransfer(" in line for line in bridge_symbols)
    assert any("QCurl::removePersistentTransfer(" in line for line in bridge_symbols)
    assert not any(
        "QCCurlMultiManager" in line for line in bridge_symbols
    ), "Other Extras 直接依赖 QCCurlMultiManager：\n" + "\n".join(
        line for line in bridge_symbols if "QCCurlMultiManager" in line
    )


def test_production_libraries_exclude_test_hook_symbols_and_strings() -> None:
    """验证正式动态库不包含测试钩子符号或稳定字符串。"""

    libraries = (
        _producer_library(f"libQCurl.so.{_project_version()}"),
        _producer_library(f"libQCurlOtherExtras.so.{_project_version()}"),
    )
    forbidden_tokens = (
        "QCURL_ENABLE_TEST_HOOKS",
        "QCURL_TEST_FORCE_",
        "ForTest",
        "ForTesting",
        "TestHook",
    )

    for library in libraries:
        assert library.is_file(), f"缺少正式动态库：{library}"

        symbols = subprocess.run(
            ["nm", "-D", "--defined-only", str(library)],
            check=False,
            capture_output=True,
            text=True,
        )
        assert symbols.returncode == 0, symbols.stderr

        strings = subprocess.run(
            ["strings", "-a", str(library)],
            check=False,
            capture_output=True,
            text=True,
        )
        assert strings.returncode == 0, strings.stderr

        exported_surface = symbols.stdout + strings.stdout
        leaked_tokens = [
            token for token in forbidden_tokens if token in exported_surface
        ]
        assert not leaked_tokens, f"正式动态库泄漏测试钩子 {leaked_tokens}：{library}"
