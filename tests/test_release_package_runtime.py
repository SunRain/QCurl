"""安装消费者的 producer 依赖身份正向与失败控制。"""

from pathlib import Path
import subprocess

import pytest

from scripts.qcurl_compatibility_common import CompatibilityError, library_environment
from scripts.release_package_runtime import consumer_runtime_libraries


def _inputs(tmp_path: Path) -> tuple[Path, dict[str, str], dict[str, Path]]:
    stage = tmp_path / "stage"
    dependencies = tmp_path / "dependencies/lib"
    dependencies.mkdir(parents=True)
    paths = {
        "libQCurl.so.2.1": stage / "lib/libQCurl.so.2.1",
        "libQt6Core.so.6": dependencies / "libQt6Core.so.6",
        "libcurl.so.4": dependencies / "libcurl.so.4",
    }
    for path in paths.values():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"dependency fixture")
    profile = {
        "Qt6Core_DIR": str(dependencies / "cmake/Qt6Core"),
        "CURL_LIBRARY_RELEASE": str(paths["libcurl.so.4"]),
        "CURL_LIBRARY_DEBUG": "CURL_LIBRARY_DEBUG-NOTFOUND",
    }
    return stage, profile, paths


def _inspect(stage: Path, profile: dict[str, str], paths: dict[str, Path]) -> dict:
    output = "".join(f"{name} => {path} (0x123)\n" for name, path in paths.items())
    return consumer_runtime_libraries(
        stage / "consumer",
        stage,
        linkage="shared",
        component="Core",
        profile=profile,
        run_command=lambda command, **kwargs: subprocess.CompletedProcess(
            command, 0, output, ""
        ),
    )


def test_consumer_records_actual_qt_curl_and_qcurl_libraries(tmp_path: Path) -> None:
    stage, profile, paths = _inputs(tmp_path)
    assert _inspect(stage, profile, paths) == {
        name: str(path.resolve()) for name, path in paths.items()
    }


@pytest.mark.parametrize("name", ["libQt6Core.so.6", "libcurl.so.4"])
@pytest.mark.parametrize("mutation", ["foreign", "unresolved", "absent"])
def test_foreign_or_missing_dependency_blocks_consumer_evidence(
    tmp_path: Path, name: str, mutation: str
) -> None:
    stage, profile, paths = _inputs(tmp_path)
    if mutation == "foreign":
        paths[name] = tmp_path / "host" / name
    elif mutation == "unresolved":
        paths[name] = Path("not found")
    else:
        paths.pop(name)
    with pytest.raises(CompatibilityError):
        _inspect(stage, profile, paths)


def test_missing_producer_library_does_not_fall_back_to_host(tmp_path: Path) -> None:
    stage, profile, paths = _inputs(tmp_path)
    profile["CURL_LIBRARY_RELEASE"] = str(tmp_path / "missing/libcurl.so")
    with pytest.raises(CompatibilityError, match="dependency is missing"):
        _inspect(stage, profile, paths)


def test_runtime_environment_discards_ambient_and_notfound_paths(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    stage, profile, paths = _inputs(tmp_path)
    monkeypatch.setenv("LD_LIBRARY_PATH", "/ambient/wrong")
    environment = library_environment(stage, profile)
    directories = environment["LD_LIBRARY_PATH"].split(":")
    assert str(paths["libcurl.so.4"].parent) in directories
    assert "/ambient/wrong" not in directories
    assert str(Path.cwd()) not in directories


@pytest.mark.parametrize("name", ["LD_PRELOAD", "LD_AUDIT"])
def test_runtime_environment_rejects_loader_injection(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, name: str
) -> None:
    stage, profile, _ = _inputs(tmp_path)
    monkeypatch.setenv(name, "/foreign/loader.so")
    with pytest.raises(CompatibilityError, match="forbidden"):
        library_environment(stage, profile)
