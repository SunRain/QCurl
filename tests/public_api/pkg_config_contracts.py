"""pkg-config shared/static dependency contract checks for the public API gate."""

from __future__ import annotations

import os
import shlex
import subprocess
from pathlib import Path

from tests.public_api.component_contracts import FailFunc


def _pkg_config_env(pc_file: Path) -> dict[str, str]:
    env = os.environ.copy()
    existing_path = env.get("PKG_CONFIG_PATH")
    pc_path = str(pc_file.parent)
    env["PKG_CONFIG_PATH"] = pc_path if not existing_path else f"{pc_path}{os.pathsep}{existing_path}"
    return env


def _pkg_config_tokens(pkg_config: str,
                       args: list[str],
                       env: dict[str, str],
                       fail_func: FailFunc) -> tuple[int, set[str]]:
    proc = subprocess.run([pkg_config, *args, "qcurl"], text=True, capture_output=True, env=env)
    if proc.returncode != 0:
        return fail_func(f"pkg-config {' '.join(args)} qcurl failed: {proc.stderr.strip()}"), set()
    return 0, set(shlex.split(proc.stdout))


def check_pkg_config_contract(stage_dir: Path, pkg_config: str, fail_func: FailFunc) -> int:
    """Verify qcurl.pc keeps shared deps minimal and exposes static private deps."""

    pc_files = sorted(stage_dir.rglob("qcurl.pc"))
    if not pc_files:
        return fail_func(f"qcurl.pc not found under {stage_dir}")

    pc_content = pc_files[0].read_text(encoding="utf-8")
    required_lines = [
        "Requires: Qt6Core >= 6.2, Qt6Network >= 6.2",
        "Requires.private: libcurl >= ",
        "Libs: -L${libdir} -lQCurl",
        "Libs.private: -lz",
    ]
    missing = [line for line in required_lines if line not in pc_content]
    if missing:
        return fail_func("qcurl.pc contract missing: " + ", ".join(missing))
    if any(line.startswith("Requires.private:") and "zlib" in line for line in pc_content.splitlines()):
        return fail_func("qcurl.pc must not duplicate zlib in Requires.private")

    env = _pkg_config_env(pc_files[0])
    rc, shared_tokens = _pkg_config_tokens(pkg_config, ["--libs"], env, fail_func)
    if rc != 0:
        return rc
    if "-lcurl" in shared_tokens or "-lz" in shared_tokens:
        return fail_func("pkg-config --libs qcurl exposed private static dependencies")

    rc, static_tokens = _pkg_config_tokens(pkg_config, ["--static", "--libs"], env, fail_func)
    if rc != 0:
        return rc
    if "-lcurl" not in static_tokens or "-lz" not in static_tokens:
        return fail_func("pkg-config --static --libs qcurl did not expose libcurl and zlib")

    print("[public_api] pkg-config contract passed")
    return 0
