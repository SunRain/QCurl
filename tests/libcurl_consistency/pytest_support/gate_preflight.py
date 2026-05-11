"""Preflight helpers for the libcurl consistency gate."""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys
from typing import Any


def first_existing_path(candidates: list[Path]) -> Path | None:
    """Return the first path that exists."""

    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def preflight_required_inputs(
    cfg: Any,
    gate_env: dict[str, str],
    planned_pytest_files: list[str],
    report: dict[str, object],
) -> None:
    """Fail before pytest when required binaries or libcurl testdeps are missing."""

    qt_bin = Path(str(gate_env.get("QCURL_QTTEST") or "")).resolve()
    libtests = first_existing_path([
        cfg.curl_build_dir / "tests" / "libtest" / "libtests",
        cfg.curl_build_dir / "tests" / "libtest" / "libtests.exe",
    ])
    preflight = {
        "qt_test_bin": str(qt_bin),
        "qt_test_exists": qt_bin.exists(),
        "libtests": str(libtests) if libtests else "",
        "libtests_exists": bool(libtests),
        "planned_pytest_files": planned_pytest_files,
    }
    report["preflight_required_inputs"] = preflight

    if not qt_bin.exists():
        raise RuntimeError(
            "QCURL_QTTEST binary missing before pytest planning executes: "
            f"{qt_bin}. Build target `tst_LibcurlConsistency` first or rerun with `--build`."
        )
    if not libtests:
        raise RuntimeError(
            "curl libtests missing before pytest planning executes: "
            f"{cfg.curl_build_dir / 'tests' / 'libtest' / 'libtests'}. "
            "Build target `qcurl_lc_deps` first or rerun with `--build`."
        )


def forbid_local_httpbin(
    cfg: Any,
    *,
    forbidden_endpoints: tuple[str, ...],
    current_file: Path,
) -> list[dict[str, object]]:
    """Return code references to forbidden local httpbin endpoints."""

    code_suffixes = {".py", ".cpp", ".cc", ".cxx", ".c", ".h", ".hpp"}
    try:
        skip_files = {current_file.resolve()}
    except OSError:
        skip_files = set()
    scan_targets: list[Path] = [
        cfg.repo_root / "tests" / "libcurl_consistency",
        cfg.repo_root / "tests" / "libcurl_consistency" / "tst_LibcurlConsistency.cpp",
    ]
    violations: list[dict[str, object]] = []

    def scan_file(path: Path) -> None:
        try:
            if path.resolve() in skip_files:
                return
        except OSError:
            return
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            return

        hits: dict[str, list[int]] = {}
        for line_no, line in enumerate(text.splitlines(), 1):
            for needle in forbidden_endpoints:
                if needle in line:
                    hits.setdefault(needle, []).append(line_no)
        if hits:
            violations.append({
                "file": str(path.relative_to(cfg.repo_root)),
                "hits": hits,
            })

    for target in scan_targets:
        if target.is_file():
            if target.suffix in code_suffixes:
                scan_file(target)
            continue
        if not target.exists():
            continue
        for path in target.rglob("*"):
            if path.is_file() and path.suffix in code_suffixes:
                scan_file(path)

    return violations


def apply_http3_preflight_to_manifest(
    cfg: Any,
    gate_env: dict[str, str],
    capability_manifest: dict[str, object],
    report: dict[str, object],
    *,
    require_http3_enabled: bool,
    run_command: Any,
) -> dict[str, object]:
    """Record HTTP/3 preflight state and disable H3-only files when unavailable."""

    preflight = report.get("preflight_http3_required")
    if not isinstance(preflight, dict):
        preflight = {
            "enabled": require_http3_enabled,
            "have_h3_server": None,
            "have_h3_curl": None,
            "violations": [],
        }
        report["preflight_http3_required"] = preflight

    nghttpx_h3 = cfg.qcurl_build_dir / "libcurl_consistency" / "nghttpx-h3" / "bin" / "nghttpx"
    have_h3_server = nghttpx_h3.exists()
    preflight["have_h3_server"] = bool(have_h3_server)
    if require_http3_enabled and not have_h3_server:
        preflight.setdefault("violations", []).append("missing_h3_server")

    if not have_h3_server:
        report["warnings"].append(
            "nghttpx-h3 not found; planner excludes test_ext_http3_success_h3.py. "
            "Build target qcurl_nghttpx_h3 for HTTP/3 coverage."
        )

    curl_bin = cfg.curl_build_dir / "src" / "curl"
    have_h3_curl: bool | None = None
    if not curl_bin.exists():
        have_h3_curl = False
        preflight["have_h3_curl"] = False
        if require_http3_enabled:
            preflight.setdefault("violations", []).append("missing_curl_bin")
        report["warnings"].append(
            "bundled curl binary not found; planner excludes HTTP/3 policy tests until qcurl_lc_deps is built."
        )
    else:
        rc: subprocess.CompletedProcess[str] = run_command(
            [str(curl_bin), "-V"],
            cwd=cfg.repo_root,
            env=gate_env,
            capture=True,
        )
        out = (rc.stdout or "") + "\n" + (rc.stderr or "")
        if out.strip():
            sys.stderr.write(out.rstrip() + "\n")
        if rc.returncode != 0:
            preflight["have_h3_curl"] = None
            if require_http3_enabled:
                preflight.setdefault("violations", []).append("curl_probe_failed")
            report["warnings"].append(f"failed to probe `curl -V` (rc={rc.returncode})")
        else:
            features_line = next((line for line in out.splitlines() if line.startswith("Features:")), "")
            have_h3_curl = any(
                tok.upper() == "HTTP3" for tok in features_line.replace("Features:", "").split()
            )
            preflight["have_h3_curl"] = bool(have_h3_curl)
            if require_http3_enabled and not have_h3_curl:
                preflight.setdefault("violations", []).append("missing_curl_http3")
            if not have_h3_curl:
                report["warnings"].append(
                    "bundled curl does not report HTTP3 in `curl -V`; planner excludes HTTP/3 policy tests."
                )

            protocols_line = next((line for line in out.splitlines() if line.startswith("Protocols:")), "")
            protocols = protocols_line.replace("Protocols:", "").split()
            ws_supported = ("ws" in protocols) or ("wss" in protocols)
            if not ws_supported:
                report["warnings"].append(
                    "bundled curl does not report ws/wss in `curl -V`; WS cases may be skipped or fail."
                )

    can_run_http3_policy = bool(have_h3_server and have_h3_curl)
    tests = capability_manifest.get("tests")
    if not isinstance(tests, dict):
        tests = {}
        capability_manifest["tests"] = tests
    entry = tests.get("test_ext_http3_success_h3.py")
    if not isinstance(entry, dict):
        entry = {}
        tests["test_ext_http3_success_h3.py"] = entry
    if not can_run_http3_policy:
        entry["enabled"] = False
        entry["reason"] = (
            "HTTP/3 preflight unavailable; default with-ext gate excludes H3 success file "
            f"(nghttpx_h3={have_h3_server}, curl_http3={preflight.get('have_h3_curl')})"
        )
    policy_entry = tests.get("test_ext_http3_version_policy.py")
    if not isinstance(policy_entry, dict):
        policy_entry = {}
        tests["test_ext_http3_version_policy.py"] = policy_entry
    policy_entry.setdefault("suite", "ext")
    policy_entry.setdefault(
        "reason",
        "HTTP/3 request policy is ext-only; planned only by run_gate.py --with-ext.",
    )
    return capability_manifest
