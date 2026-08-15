#!/usr/bin/env python3
"""生成绑定 release tree 的 Doxygen HTML 证据。"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path
from typing import Callable

if __package__:
    from . import generate_doxygen_input_from_surface_manifest as generate_input
else:
    import generate_doxygen_input_from_surface_manifest as generate_input


RunCommand = Callable[..., subprocess.CompletedProcess[object]]


def _replace_setting(config: str, name: str, value: str) -> str:
    """替换唯一 Doxygen 配置项，缺失或重复时失败。"""

    pattern = re.compile(rf"(?m)^\s*{re.escape(name)}\s*=.*$")
    rendered, count = pattern.subn(f'{name} = "{value}"', config)
    if count != 1:
        raise ValueError(f"Doxygen setting must occur exactly once: {name}")
    return rendered


def _run_command(command: list[str], *, cwd: Path) -> subprocess.CompletedProcess[object]:
    return subprocess.run(command, cwd=cwd, check=False)


def run_gate(
    repo_root: Path,
    output_dir: Path,
    *,
    doxygen: str,
    run_command: RunCommand = _run_command,
) -> int:
    """生成 public input 和 HTML，并要求稳定入口文件存在。"""

    repo_root = repo_root.resolve()
    output_dir = output_dir.resolve()
    input_path = output_dir / "qcurl_api_input.doxy"
    generate_input.main(
        [
            "--manifest",
            str(repo_root / "tests" / "public_api" / "surface_manifest.json"),
            "--source-dir",
            str(repo_root / "src"),
            "--output",
            str(input_path),
            "--check",
        ]
    )

    config = (repo_root / "Doxyfile").read_text(encoding="utf-8")
    config = _replace_setting(config, "OUTPUT_DIRECTORY", str(output_dir.parent))
    config = _replace_setting(config, "HTML_OUTPUT", output_dir.name)
    config = _replace_setting(config, "@INCLUDE", str(input_path))
    config_path = output_dir / "qcurl.release.doxyfile"
    config_path.write_text(config, encoding="utf-8")

    completed = run_command([doxygen, str(config_path)], cwd=repo_root)
    if completed.returncode != 0:
        return int(completed.returncode)
    index_path = output_dir / "index.html"
    if not index_path.is_file() or index_path.stat().st_size == 0:
        raise RuntimeError(f"Doxygen HTML report was not produced: {index_path}")
    return 0


def main(argv: list[str] | None = None) -> int:
    """解析命令行并执行 Doxygen release gate。"""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--doxygen", default="doxygen")
    args = parser.parse_args(argv)
    repo_root = Path(__file__).resolve().parent.parent
    try:
        return run_gate(
            repo_root,
            args.output_dir,
            doxygen=args.doxygen,
        )
    except (OSError, RuntimeError, UnicodeDecodeError, ValueError) as exc:
        print(f"[doxygen_gate] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
