"""Exercise CMake's real SameMinorVersion and pkg-config's independent constraints."""

from pathlib import Path
import subprocess

import pytest

CONSUMER = Path(__file__).resolve().parent / "consumer_version"


@pytest.fixture(params=["2.1.0", "2.1.2", "2.1.3", "2.2.0"])
def package_version_fixture(tmp_path: Path, request) -> tuple[Path, str]:
    """Use CMake's standard generator, without claiming synthetic versions were released."""

    version = request.param
    prefix = tmp_path / "prefix"
    package = prefix / "lib/cmake/QCurl"
    package.mkdir(parents=True)
    (package / "QCurlConfig.cmake").write_text(
        f'set(QCurl_VERSION "{version}")\n', encoding="utf-8"
    )
    generator = tmp_path / "generate.cmake"
    generator.write_text(
        "include(CMakePackageConfigHelpers)\n"
        f'write_basic_package_version_file("{package / "QCurlConfigVersion.cmake"}"\n'
        f"  VERSION {version} COMPATIBILITY SameMinorVersion ARCH_INDEPENDENT)\n",
        encoding="utf-8",
    )
    subprocess.run(
        ["cmake", "-P", str(generator)], check=True, capture_output=True, text=True
    )
    return prefix, version


@pytest.mark.parametrize(
    "minimum", ["2.1.0", "2.1.2", "2.1.3", "2.0.0", "2.2.0", "2.2.1", "2.3.0", "3.0.0"]
)
def test_cmake_minimum_request_stays_in_same_major_minor(
    tmp_path: Path, package_version_fixture: tuple[Path, str], minimum: str
) -> None:
    prefix, candidate = package_version_fixture
    requested_version = tuple(map(int, minimum.split(".")))
    installed_version = tuple(map(int, candidate.split(".")))
    expected = (
        requested_version[:2] == installed_version[:2]
        and installed_version >= requested_version
    )
    result = subprocess.run(
        [
            "cmake",
            "-S",
            str(CONSUMER),
            "-B",
            str(tmp_path / f"request-{minimum}"),
            f"-DQCURL_STAGE_PREFIX={prefix}",
            f"-DQCURL_REQUEST_VERSION={minimum}",
            f"-DQCURL_EXPECTED_VERSION={candidate}",
        ],
        capture_output=True,
        text=True,
    )
    assert (result.returncode == 0) is expected, result.stdout + result.stderr
    if not expected:
        assert "not compatible" in result.stderr


@pytest.mark.parametrize("line", ["2.1", "2.2"])
@pytest.mark.parametrize("minor_offset,patch", [(-1, 9), (0, 0), (0, 3), (1, 0)])
def test_pkg_config_requires_explicit_lower_and_upper_bounds(
    tmp_path: Path, monkeypatch, line: str, minor_offset: int, patch: int
) -> None:
    major, minor = map(int, line.split("."))
    candidate = f"{major}.{minor + minor_offset}.{patch}"
    accepted = minor_offset == 0
    (tmp_path / "qcurl.pc").write_text(
        f"Name: qcurl\nDescription: matching rule fixture\nVersion: {candidate}\n",
        encoding="utf-8",
    )
    monkeypatch.setenv("PKG_CONFIG_LIBDIR", str(tmp_path))
    monkeypatch.delenv("PKG_CONFIG_PATH", raising=False)
    result = subprocess.run(
        ["pkg-config", "--exists", f"qcurl >= {line}.0", f"qcurl < {major}.{minor + 1}.0"]
    )
    assert (result.returncode == 0) is accepted


def test_cmake_exact_does_not_mean_same_line_minimum(
    tmp_path: Path, package_version_fixture: tuple[Path, str]
) -> None:
    """EXACT accepts only the requested patch, unlike the recommended minimum request."""

    prefix, candidate = package_version_fixture
    result = subprocess.run(
        [
            "cmake",
            "-S",
            str(CONSUMER),
            "-B",
            str(tmp_path / "exact"),
            f"-DQCURL_STAGE_PREFIX={prefix}",
            "-DQCURL_REQUEST_VERSION=2.1.0",
            "-DQCURL_REQUEST_EXACT=ON",
            f"-DQCURL_EXPECTED_VERSION={candidate}",
        ],
        capture_output=True,
        text=True,
    )
    assert (result.returncode == 0) is (candidate == "2.1.0"), (
        result.stdout + result.stderr
    )
    if result.returncode:
        assert "exactly matches" in " ".join(result.stderr.split())
