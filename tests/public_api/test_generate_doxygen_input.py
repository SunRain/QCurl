from __future__ import annotations

import importlib.util
from pathlib import Path


def _load_module(repo_root: Path):
    script = repo_root / "scripts" / "generate_doxygen_input_from_surface_manifest.py"
    spec = importlib.util.spec_from_file_location("generate_doxygen_input_from_surface_manifest", script)
    assert spec is not None
    assert spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)  # type: ignore[attr-defined]
    return module


def test_generate_doxygen_input_from_surface_manifest_excludes_internal_headers(tmp_path: Path) -> None:
    repo_root = Path(__file__).resolve().parents[2]
    module = _load_module(repo_root)

    src = tmp_path / "src"
    src.mkdir()
    for header in [
        "QCNetworkAccessManager.h",
        "QCNetworkDiagnostics.h",
        "QCNetworkReply_p.h",
    ]:
        (src / header).write_text("// header\n", encoding="utf-8")

    manifest = tmp_path / "surface_manifest.json"
    manifest.write_text(
        """
{
  "schemaVersion": 1,
  "layers": ["Core", "Other Extras", "Internal"],
  "headers": [
    {
      "path": "QCNetworkAccessManager.h",
      "layer": "Core",
      "currentInstall": "core-default",
      "targetInstall": "core-default"
    },
    {
      "path": "QCNetworkDiagnostics.h",
      "layer": "Other Extras",
      "currentInstall": "other-extras",
      "targetInstall": "other-extras"
    },
    {
      "path": "QCNetworkReply_p.h",
      "layer": "Internal",
      "currentInstall": "internal",
      "targetInstall": "internal"
    }
  ]
}
""",
        encoding="utf-8",
    )
    output = tmp_path / "build" / "doxygen" / "input.doxy"

    rc = module.main(
        [
            "--manifest",
            str(manifest),
            "--source-dir",
            str(src),
            "--output",
            str(output),
            "--check",
        ]
    )

    assert rc == 0
    rendered = output.read_text(encoding="utf-8")
    assert "Source manifest" in rendered
    assert f"{src.as_posix()}/QCNetworkAccessManager.h" in rendered
    assert f"{src.as_posix()}/QCNetworkDiagnostics.h" in rendered
    assert "QCNetworkReply_p.h" not in rendered
    assert "INPUT =" in rendered
