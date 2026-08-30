from __future__ import annotations

import subprocess
from pathlib import Path

from scripts import run_doxygen_gate


def test_release_doxygen_gate_writes_html_to_bound_output(
    tmp_path: Path,
) -> None:
    repo = tmp_path / "repo"
    source = repo / "src"
    source.mkdir(parents=True)
    (source / "Public.h").write_text("/// public\n", encoding="utf-8")
    manifest = repo / "tests" / "public_api" / "surface_manifest.json"
    manifest.parent.mkdir(parents=True)
    manifest.write_text(
        '{"headers":[{"path":"Public.h","layer":"Core",'
        '"currentInstall":"core-component"}]}\n',
        encoding="utf-8",
    )
    (repo / "Doxyfile").write_text(
        "OUTPUT_DIRECTORY = build\n"
        "HTML_OUTPUT = doxygen\n"
        "@INCLUDE = build/doxygen/qcurl_api_input.doxy\n",
        encoding="utf-8",
    )
    output_dir = tmp_path / "release-shared" / "evidence" / "doxygen"

    def run_command(command: list[str], *, cwd: Path):
        assert command[0] == "doxygen"
        config = Path(command[1]).read_text(encoding="utf-8")
        assert f'OUTPUT_DIRECTORY = "{output_dir.parent}"' in config
        assert f'HTML_OUTPUT = "{output_dir.name}"' in config
        output_dir.mkdir(parents=True, exist_ok=True)
        (output_dir / "index.html").write_text(
            "<!doctype html><html></html>\n",
            encoding="utf-8",
        )
        return subprocess.CompletedProcess(command, 0)

    result = run_doxygen_gate.run_gate(
        repo,
        output_dir,
        doxygen="doxygen",
        run_command=run_command,
    )

    assert result == 0
    assert (output_dir / "index.html").is_file()
