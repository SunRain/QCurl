"""pkg-config shared/static dependency contract checks for the public API gate."""

from __future__ import annotations

import os
import shlex
import subprocess
from pathlib import Path

from tests.public_api.component_contracts import FailFunc


def _pkg_config_env(pc_dirs: list[Path]) -> dict[str, str]:
    env = os.environ.copy()
    existing_path = env.get("PKG_CONFIG_PATH")
    pc_path = os.pathsep.join(str(pc_dir) for pc_dir in pc_dirs)
    env["PKG_CONFIG_PATH"] = pc_path if not existing_path else f"{pc_path}{os.pathsep}{existing_path}"
    return env


def _pkg_config_tokens(pkg_config: str,
                       package: str,
                       args: list[str],
                       env: dict[str, str],
                       fail_func: FailFunc) -> tuple[int, set[str]]:
    proc = subprocess.run([pkg_config, *args, package], text=True, capture_output=True, env=env)
    if proc.returncode != 0:
        return fail_func(
            f"pkg-config {' '.join(args)} {package} failed: {proc.stderr.strip()}"
        ), set()
    return 0, set(shlex.split(proc.stdout))


def check_pkg_config_contract(
    stage_dir: Path,
    pkg_config: str,
    fail_func: FailFunc,
    *,
    other_extras_stage_dir: Path | None = None,
) -> int:
    """Verify qcurl.pc keeps shared deps minimal and exposes static private deps."""

    core_pc_files = sorted(stage_dir.rglob("qcurl.pc"))
    if not core_pc_files:
        return fail_func(f"qcurl.pc not found under {stage_dir}")
    other_extras_stage = other_extras_stage_dir or stage_dir
    other_extras_pc_files = sorted(other_extras_stage.rglob("qcurl-other-extras.pc"))
    if not other_extras_pc_files:
        return fail_func(f"qcurl-other-extras.pc not found under {other_extras_stage}")

    core_pc = core_pc_files[0]
    core_pc_content = core_pc.read_text(encoding="utf-8")
    required_core_lines = [
        "Requires: Qt6Core >= 6.2, Qt6Network >= 6.2",
        "Requires.private: libcurl >= ",
        "Libs: -L${libdir} -lQCurl",
    ]
    missing = [line for line in required_core_lines if line not in core_pc_content]
    if missing:
        return fail_func("qcurl.pc contract missing: " + ", ".join(missing))
    if "-lz" in core_pc_content or "zlib" in core_pc_content.lower():
        return fail_func("qcurl.pc must not carry zlib for the default Core contract")

    other_extras_pc_content = other_extras_pc_files[0].read_text(encoding="utf-8")
    required_other_extras_lines = [
        "Requires: qcurl = ",
        "Requires.private: zlib",
        "Libs: -L${libdir} -lQCurlOtherExtras",
    ]
    missing = [
        line for line in required_other_extras_lines if line not in other_extras_pc_content
    ]
    if missing:
        return fail_func("qcurl-other-extras.pc contract missing: " + ", ".join(missing))

    pc_dirs = [core_pc.parent]
    other_extras_pc_dir = other_extras_pc_files[0].parent
    if other_extras_pc_dir not in pc_dirs:
        pc_dirs.insert(0, other_extras_pc_dir)
    env = _pkg_config_env(pc_dirs)
    rc, shared_tokens = _pkg_config_tokens(pkg_config, "qcurl", ["--libs"], env, fail_func)
    if rc != 0:
        return rc
    if "-lcurl" in shared_tokens or "-lz" in shared_tokens:
        return fail_func("pkg-config --libs qcurl exposed private static dependencies")

    rc, static_tokens = _pkg_config_tokens(
        pkg_config, "qcurl", ["--static", "--libs"], env, fail_func
    )
    if rc != 0:
        return rc
    if "-lcurl" not in static_tokens:
        return fail_func("pkg-config --static --libs qcurl did not expose libcurl")

    rc, other_extras_shared_tokens = _pkg_config_tokens(
        pkg_config, "qcurl-other-extras", ["--libs"], env, fail_func
    )
    if rc != 0:
        return rc
    if "-lQCurlOtherExtras" not in other_extras_shared_tokens:
        return fail_func("pkg-config --libs qcurl-other-extras did not expose QCurlOtherExtras")
    if "-lz" in other_extras_shared_tokens:
        return fail_func("pkg-config --libs qcurl-other-extras exposed private zlib dependency")

    rc, other_extras_static_tokens = _pkg_config_tokens(
        pkg_config, "qcurl-other-extras", ["--static", "--libs"], env, fail_func
    )
    if rc != 0:
        return rc
    if "-lQCurlOtherExtras" not in other_extras_static_tokens:
        return fail_func("pkg-config --static --libs qcurl-other-extras did not expose QCurlOtherExtras")
    if "-lz" not in other_extras_static_tokens:
        return fail_func("pkg-config --static --libs qcurl-other-extras did not expose zlib")

    print("[public_api] pkg-config contract passed")
    return 0
