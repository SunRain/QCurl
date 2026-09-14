from __future__ import annotations

import os
from pathlib import Path
import subprocess

import pytest
import yaml


QT_ACTION = Path(".github/actions/setup-qt/action.yml")
SDK_JOBS = [
    ("benchmark.yml", "benchmark"),
    ("pr_fast_gate.yml", "pr_fast_gate"),
    ("uce_nightly.yml", "uce_nightly"),
    ("uce_soak.yml", "uce_soak"),
    ("libcurl_consistency_ext_gate.yml", "libcurl_consistency_gate"),
    ("release_delivery_http3_gate.yml", "release_debian12"),
]


def _version_script() -> str:
    action = yaml.load(QT_ACTION.read_text(encoding="utf-8"), Loader=yaml.BaseLoader)
    return next(step["run"] for step in action["runs"]["steps"] if step.get("id") == "qt-version")


@pytest.mark.parametrize("version", ["6.10.3", "6.11.0"])
def test_qt_setup_reads_version_from_cmake(tmp_path: Path, version: str) -> None:
    (tmp_path / "CMakeLists.txt").write_text(
        f'set(QCURL_MIN_QT_VERSION "{version}")\n', encoding="utf-8"
    )
    output = tmp_path / "github-output"
    completed = subprocess.run(
        ["bash", "-c", _version_script()],
        cwd=tmp_path,
        env={**os.environ, "GITHUB_OUTPUT": str(output)},
        capture_output=True,
        text=True,
    )
    assert completed.returncode == 0, completed.stderr
    assert output.read_text(encoding="utf-8") == f"version={version}\n"


@pytest.mark.parametrize(
    "source",
    ["", 'set(QCURL_MIN_QT_VERSION "6.10")\n', 'set(QCURL_MIN_QT_VERSION "6.10.3")\n' * 2],
)
def test_qt_setup_does_not_fall_back_to_installer_default(tmp_path: Path, source: str) -> None:
    (tmp_path / "CMakeLists.txt").write_text(source, encoding="utf-8")
    output = tmp_path / "github-output"
    completed = subprocess.run(
        ["bash", "-c", _version_script()],
        cwd=tmp_path,
        env={**os.environ, "GITHUB_OUTPUT": str(output)},
        capture_output=True,
        text=True,
    )
    assert completed.returncode == 1
    assert not output.exists()


@pytest.mark.parametrize(("workflow", "job"), SDK_JOBS)
def test_ubuntu_and_debian_jobs_install_the_project_sdk(workflow: str, job: str) -> None:
    source = (Path(".github/workflows") / workflow).read_text(encoding="utf-8")
    config = yaml.load(source, Loader=yaml.BaseLoader)
    steps = config["jobs"][job]["steps"]
    sdk_index = next(index for index, step in enumerate(steps) if step.get("uses") == "./.github/actions/setup-qt")
    checkout = next(index for index, step in enumerate(steps) if step.get("uses", "").startswith("actions/checkout@"))
    configure = next(index for index, step in enumerate(steps) if "cmake -" in step.get("run", ""))
    assert checkout < sdk_index < configure
    assert "qt6-base-dev" not in source
    if job == "release_debian12":
        assert steps[sdk_index]["with"]["install-deps"] == "nosudo"


def test_qt_installer_consumes_the_single_version_source() -> None:
    action = yaml.load(QT_ACTION.read_text(encoding="utf-8"), Loader=yaml.BaseLoader)
    install = next(step for step in action["runs"]["steps"] if "uses" in step)
    assert install["with"]["version"] == "${{ steps.qt-version.outputs.version }}"
    assert install["with"]["install-deps"] == "${{ inputs.install-deps }}"
