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
    additional_excluded_files: tuple[Path, ...] = (),
) -> list[dict[str, object]]:
    """Return code references to forbidden local httpbin endpoints."""

    code_suffixes = {".py", ".cpp", ".cc", ".cxx", ".c", ".h", ".hpp"}
    try:
        skip_files = {current_file.resolve()}
        skip_files.update(path.resolve() for path in additional_excluded_files)
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


def _probe_curl_http3(
    cfg: Any,
    gate_env: dict[str, str],
    *,
    require_http3_enabled: bool,
    run_command: Any,
) -> tuple[bool | None, list[str], list[str]]:
    """探测 bundled curl 的 HTTP/3 能力并返回违规与告警。"""

    violations: list[str] = []
    warnings: list[str] = []
    curl_bin = cfg.curl_build_dir / "src" / "curl"
    if not curl_bin.exists():
        if require_http3_enabled:
            violations.append("missing_curl_bin")
        warnings.append(
            "bundled curl binary not found; planner excludes HTTP/3 policy tests until qcurl_lc_deps is built."
        )
        return False, violations, warnings
    rc: subprocess.CompletedProcess[str] = run_command(
        [str(curl_bin), "-V"], cwd=cfg.repo_root, env=gate_env, capture=True
    )
    out = (rc.stdout or "") + "\n" + (rc.stderr or "")
    if out.strip():
        sys.stderr.write(out.rstrip() + "\n")
    if rc.returncode != 0:
        if require_http3_enabled:
            violations.append("curl_probe_failed")
        warnings.append(f"failed to probe `curl -V` (rc={rc.returncode})")
        return None, violations, warnings
    features = next((line for line in out.splitlines() if line.startswith("Features:")), "")
    have_http3 = any(token.upper() == "HTTP3" for token in features.split()[1:])
    if require_http3_enabled and not have_http3:
        violations.append("missing_curl_http3")
    if not have_http3:
        warnings.append(
            "bundled curl does not report HTTP3 in `curl -V`; planner excludes HTTP/3 policy tests."
        )
    protocols = next((line for line in out.splitlines() if line.startswith("Protocols:")), "")
    protocol_names = protocols.split()[1:]
    if "ws" not in protocol_names and "wss" not in protocol_names:
        warnings.append("bundled curl does not report ws/wss in `curl -V`; WS cases may be skipped or fail.")
    return have_http3, violations, warnings


def evaluate_http3_preflight(
    cfg: Any,
    gate_env: dict[str, str],
    *,
    require_http3_enabled: bool,
    run_command: Any,
) -> dict[str, object]:
    """生成独立的 HTTP/3 环境事实与 planner 覆盖规则。"""

    server = cfg.qcurl_build_dir / "libcurl_consistency/nghttpx-h3/bin/nghttpx"
    have_server = server.exists()
    violations = ["missing_h3_server"] if require_http3_enabled and not have_server else []
    warnings = [] if have_server else [
        "nghttpx-h3 not found; planner excludes test_ext_http3_success_h3.py. "
        "Build target qcurl_nghttpx_h3 for HTTP/3 coverage."
    ]
    have_curl, curl_violations, curl_warnings = _probe_curl_http3(
        cfg,
        gate_env,
        require_http3_enabled=require_http3_enabled,
        run_command=run_command,
    )
    violations.extend(curl_violations)
    warnings.extend(curl_warnings)
    overrides: dict[str, object] = {}
    if not bool(have_server and have_curl):
        overrides["test_ext_http3_success_h3.py"] = {
            "enabled": False,
            "reason": (
                "HTTP/3 preflight unavailable; default with-ext gate excludes H3 success file "
                f"(nghttpx_h3={have_server}, curl_http3={have_curl})"
            ),
        }
    return {
        "enabled": require_http3_enabled,
        "have_h3_server": have_server,
        "have_h3_curl": have_curl,
        "violations": violations,
        "warnings": warnings,
        "planner_overrides": overrides,
    }
