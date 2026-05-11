"""Reproducibility evidence helpers for the libcurl consistency gate."""

from __future__ import annotations

from pathlib import Path
from typing import Any
import hashlib

from tests.libcurl_consistency.pytest_support.gate_report import redact_text


def add_python_lock_summary(report: dict[str, Any], repo_root: Path) -> None:
    """Record the Python lock file inputs used by the gate."""

    lock_path = repo_root / "tests" / "libcurl_consistency" / "requirements.lock.txt"
    report["python_deps_lock"] = {
        "path": str(lock_path),
        "exists": lock_path.exists(),
        "install_mode": "python3 -m pip install -r tests/libcurl_consistency/requirements.lock.txt",
    }
    try:
        if lock_path.exists():
            raw = lock_path.read_bytes()
            report["python_deps_lock"]["sha256"] = hashlib.sha256(raw).hexdigest()
            report["python_deps_lock"]["lines"] = len(raw.splitlines())
    except Exception as exc:
        report["warnings"].append(f"failed to read requirements.lock.txt for sha256: {exc}")


def write_python_env_snapshot(cfg: Any, gate_env: dict[str, str], report: dict[str, Any], *, run_command: Any) -> None:
    """Write python version and pip freeze snapshots for audit replay."""

    try:
        reports_dir = cfg.json_report.parent
        py_ver = run_command(["python3", "--version"], cwd=cfg.repo_root, env=gate_env, capture=True)
        pip_freeze = run_command(["python3", "-m", "pip", "freeze"], cwd=cfg.repo_root, env=gate_env, capture=True)
        env_text = [
            "# python environment snapshot (for reproducibility/audit)\n",
            f"python3 --version: {(py_ver.stdout or py_ver.stderr or '').strip()}\n",
            "\n# pip freeze\n",
            redact_text((pip_freeze.stdout or "").strip()),
            "\n",
        ]
        env_path = reports_dir / f"pip_freeze_{cfg.suite}.txt"
        env_path.write_text("".join(env_text), encoding="utf-8")
        report["pip_freeze_artifact"] = str(env_path)
    except Exception as exc:
        report["warnings"].append(f"failed to write pip_freeze artifact: {exc}")


def write_nghttpx_version_snapshot(
    cfg: Any,
    gate_env: dict[str, str],
    report: dict[str, Any],
    *,
    run_command: Any,
) -> None:
    """Write the nghttpx-h3 version snapshot when the tool exists."""

    try:
        reports_dir = cfg.json_report.parent
        nghttpx_h3_bin = cfg.qcurl_build_dir / "libcurl_consistency" / "nghttpx-h3" / "bin" / "nghttpx"
        if not nghttpx_h3_bin.exists():
            return
        ng_ver = run_command([str(nghttpx_h3_bin), "--version"], cwd=cfg.repo_root, env=gate_env, capture=True)
        out = (ng_ver.stdout or "") + "\n" + (ng_ver.stderr or "")
        text = [
            "# nghttpx-h3 version snapshot (for reproducibility/audit)\n\n",
            f"command: {nghttpx_h3_bin} --version\n",
            f"returncode: {ng_ver.returncode}\n\n",
            out.strip() + "\n" if out.strip() else "<no output>\n",
        ]
        ng_path = reports_dir / f"nghttpx_version_{cfg.suite}.txt"
        ng_path.write_text(redact_text("".join(text)), encoding="utf-8")
        report["nghttpx_version_artifact"] = str(ng_path)
    except Exception as exc:
        report["warnings"].append(f"failed to write nghttpx version artifact: {exc}")
