"""pkg-config shared/static dependency contract checks for the public API gate."""

from __future__ import annotations

import os
import re
import shlex
import subprocess
from pathlib import Path

from tests.public_api.component_contracts import FailFunc


def _pkg_config_env(pc_dirs: list[Path]) -> dict[str, str]:
    env = os.environ.copy()
    existing_path = env.get("PKG_CONFIG_PATH")
    pc_path = os.pathsep.join(str(pc_dir) for pc_dir in pc_dirs)
    env["PKG_CONFIG_PATH"] = (
        pc_path if not existing_path else f"{pc_path}{os.pathsep}{existing_path}"
    )
    return env


def _pkg_config_tokens(
    pkg_config: str,
    package: str,
    args: list[str],
    env: dict[str, str],
    fail_func: FailFunc,
) -> tuple[int, set[str]]:
    proc = subprocess.run(
        [pkg_config, *args, package], text=True, capture_output=True, env=env
    )
    if proc.returncode != 0:
        return (
            fail_func(
                f"pkg-config {' '.join(args)} {package} failed: {proc.stderr.strip()}"
            ),
            set(),
        )
    return 0, set(shlex.split(proc.stdout))


def _core_metadata_error(content: str) -> str | None:
    required_core_lines = [
        "Requires: Qt6Core >= 6.10.3",
        "Requires.private: libcurl >= ",
        "Libs: -L${libdir} -lQCurl",
    ]
    missing = [line for line in required_core_lines if line not in content]
    if missing:
        return "qcurl.pc contract missing: " + ", ".join(missing)
    if "-lz" in content or "zlib" in content.lower():
        return "qcurl.pc must not carry zlib for the default Core contract"
    if "Qt6Network" in content:
        return "qcurl.pc must not carry Qt6Network for the default Core contract"

    return None


def _other_extras_metadata_error(
    core_pc_content: str, other_extras_pc_content: str
) -> str | None:
    required_other_extras_lines = [
        "Requires: qcurl = ",
        "Qt6Network >= 6.10.3",
        "Requires.private: zlib",
        "Libs: -L${libdir} -lQCurlOtherExtras",
    ]
    missing = [
        line
        for line in required_other_extras_lines
        if line not in other_extras_pc_content
    ]
    if missing:
        return "qcurl-other-extras.pc contract missing: " + ", ".join(missing)

    versions = [
        re.search(r"^Version:\s*(\d+\.\d+\.\d+)\s*$", content, re.MULTILINE)
        for content in (core_pc_content, other_extras_pc_content)
    ]
    if not all(versions) or versions[0].group(1) != versions[1].group(1):
        return "pkg-config components must declare the same full release version"
    version = versions[0].group(1)
    if not re.search(
        rf"^Requires:\s*qcurl\s*=\s*{re.escape(version)}(?:,|\s*$)",
        other_extras_pc_content,
        re.MULTILINE,
    ):
        return "Other Extras must require the exact same Core package version"

    return None


def _check_core_tokens(
    pkg_config: str, env: dict[str, str], fail_func: FailFunc
) -> int:
    rc, shared_tokens = _pkg_config_tokens(
        pkg_config, "qcurl", ["--libs"], env, fail_func
    )
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

    return 0


def _check_other_extras_tokens(
    pkg_config: str, env: dict[str, str], fail_func: FailFunc
) -> int:
    rc, other_extras_shared_tokens = _pkg_config_tokens(
        pkg_config, "qcurl-other-extras", ["--libs"], env, fail_func
    )
    if rc != 0:
        return rc
    if "-lQCurlOtherExtras" not in other_extras_shared_tokens:
        return fail_func(
            "pkg-config --libs qcurl-other-extras did not expose QCurlOtherExtras"
        )
    if "-lz" in other_extras_shared_tokens:
        return fail_func(
            "pkg-config --libs qcurl-other-extras exposed private zlib dependency"
        )

    rc, other_extras_static_tokens = _pkg_config_tokens(
        pkg_config, "qcurl-other-extras", ["--static", "--libs"], env, fail_func
    )
    if rc != 0:
        return rc
    if "-lQCurlOtherExtras" not in other_extras_static_tokens:
        return fail_func(
            "pkg-config --static --libs qcurl-other-extras did not expose QCurlOtherExtras"
        )
    if "-lz" not in other_extras_static_tokens:
        return fail_func(
            "pkg-config --static --libs qcurl-other-extras did not expose zlib"
        )

    return 0


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
    error = _core_metadata_error(core_pc_content)
    if error is not None:
        return fail_func(error)
    other_extras_pc_content = other_extras_pc_files[0].read_text(encoding="utf-8")
    error = _other_extras_metadata_error(core_pc_content, other_extras_pc_content)
    if error is not None:
        return fail_func(error)

    pc_dirs = [core_pc.parent]
    other_extras_pc_dir = other_extras_pc_files[0].parent
    if other_extras_pc_dir not in pc_dirs:
        pc_dirs.insert(0, other_extras_pc_dir)
    env = _pkg_config_env(pc_dirs)
    rc = _check_core_tokens(pkg_config, env, fail_func)
    if rc != 0:
        return rc
    rc = _check_other_extras_tokens(pkg_config, env, fail_func)
    if rc != 0:
        return rc

    print("[public_api] pkg-config contract passed")
    return 0
