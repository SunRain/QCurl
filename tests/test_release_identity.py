from __future__ import annotations

import hashlib
import json
import subprocess
from pathlib import Path

import pytest

from scripts import release_identity


def _git(repo: Path, *args: str, input: str | None = None) -> str:
    return subprocess.run(
        [
            "git",
            "-c",
            "protocol.file.allow=always",
            "-c",
            "user.name=Identity Test",
            "-c",
            "user.email=identity@example.invalid",
            *args,
        ],
        cwd=repo,
        input=input,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.rstrip("\n")


def _init_repo(path: Path) -> Path:
    path.mkdir()
    _git(path, "init", "-q")
    (path / "subject.txt").write_text("initial\n", encoding="utf-8")
    _git(path, "add", "subject.txt")
    _git(path, "commit", "-qm", "initial")
    return path


def _expected_digest(value: object) -> str:
    encoded = json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode()
    return hashlib.sha256(encoded).hexdigest()


def _assert_digest(identity: dict) -> None:
    assert identity["digest"] == _expected_digest(identity["entries"])


@pytest.fixture
def submodule_repo(tmp_path: Path) -> Path:
    leaf = _init_repo(tmp_path / "leaf")
    parent = _init_repo(tmp_path / "parent")
    _git(parent, "submodule", "add", "-q", str(leaf), "nested")
    _git(parent, "commit", "-qam", "nested submodule")
    repo = _init_repo(tmp_path / "repo")
    _git(repo, "submodule", "add", "-q", str(leaf), "alpha")
    _git(repo, "submodule", "add", "-q", str(parent), "beta")
    _git(repo, "submodule", "update", "--init", "--recursive")
    _git(repo, "commit", "-qam", "submodules")
    return repo


def test_identity_preserves_first_and_consecutive_clean_recursive_entries(
    submodule_repo: Path,
) -> None:
    repo = submodule_repo
    assert _git(repo, "submodule", "status", "--recursive").startswith(" ")
    identity = release_identity._submodules(repo)
    assert [entry["path"] for entry in identity["entries"]] == [
        "alpha",
        "beta",
        "beta/nested",
    ]
    for entry in identity["entries"]:
        path = Path(entry["path"])
        expected_sha = _git(repo / path.parent, "rev-parse", f":{path.name}")
        assert entry["index_sha"] == expected_sha
        assert entry["sha"] == _git(repo / path, "rev-parse", "HEAD")
        assert entry["dirty"] is False
        assert entry["tracked_patch"] == {
            "patch_digest": hashlib.sha256(b"").hexdigest(),
            "content_digest": _expected_digest([]),
            "entries": [],
        }
        assert entry["untracked"] == {"digest": _expected_digest([]), "entries": []}
    _assert_digest(identity)


def _advance_checkout(repo: Path) -> tuple[str, str]:
    before = _git(repo, "rev-parse", ":alpha")
    child = repo / "alpha"
    (child / "subject.txt").write_text("new commit\n", encoding="utf-8")
    _git(child, "commit", "-qam", "advance checkout")
    return before, _git(child, "rev-parse", "HEAD")


def test_identity_records_index_and_checkout_shas_for_plus_marker(
    submodule_repo: Path,
) -> None:
    repo = submodule_repo
    before = release_identity._submodules(repo)
    index_sha, checkout_sha = _advance_checkout(repo)
    assert _git(repo, "submodule", "status", "--recursive").startswith("+")
    assert not _git(repo / "alpha", "status", "--porcelain")
    after = release_identity._submodules(repo)
    entry = after["entries"][0]
    assert len(after["entries"]) == 3
    assert entry["path"] == "alpha"
    assert entry["index_sha"] == index_sha
    assert entry["sha"] == checkout_sha != index_sha
    assert entry["dirty"] is True
    assert before["digest"] != after["digest"]
    _assert_digest(after)


def test_identity_tracks_dirty_nested_content_and_untracked_bytes(
    submodule_repo: Path,
) -> None:
    repo = submodule_repo
    before = release_identity._submodules(repo)
    child = repo / "beta/nested"
    (child / "subject.txt").write_text("tracked change\n", encoding="utf-8")
    untracked = child / "extra.txt"
    untracked.write_text("first\n", encoding="utf-8")
    after = release_identity._submodules(repo)
    assert len(after["entries"]) == 3
    nested = after["entries"][2]
    assert nested["path"] == "beta/nested"
    assert nested["index_sha"] == _git(repo / "beta", "rev-parse", ":nested")
    assert nested["sha"] == _git(child, "rev-parse", "HEAD")
    assert nested["dirty"] is True
    assert nested["tracked_patch"]["entries"][0]["path"] == "subject.txt"
    assert nested["untracked"]["entries"][0]["path"] == "extra.txt"
    assert before["digest"] != after["digest"]
    untracked.write_text("second\n", encoding="utf-8")
    changed = release_identity._submodules(repo)
    assert changed["digest"] != after["digest"]
    assert changed["entries"][2]["untracked"]["digest"] != nested["untracked"]["digest"]
    _assert_digest(changed)


def test_identity_marks_uninitialized_checkout_missing_not_parent_head(
    submodule_repo: Path,
) -> None:
    repo = submodule_repo
    before = release_identity._submodules(repo)
    index_sha = _git(repo, "rev-parse", ":alpha")
    _git(repo, "submodule", "deinit", "-f", "alpha")
    assert (repo / "alpha").is_dir()
    assert _git(repo, "submodule", "status", "--recursive").startswith("-")
    after = release_identity._submodules(repo)
    assert len(after["entries"]) == 3
    assert after["entries"][0] == {
        "path": "alpha",
        "index_sha": index_sha,
        "sha": "missing",
        "dirty": True,
        "tracked_patch": None,
        "untracked": None,
    }
    assert before["digest"] != after["digest"]
    _assert_digest(after)


def test_identity_marks_unmerged_gitlink_dirty(submodule_repo: Path) -> None:
    repo = submodule_repo
    before = release_identity._submodules(repo)
    index_sha, checkout_sha = _advance_checkout(repo)
    _git(
        repo,
        "update-index",
        "--index-info",
        input=(
            f"0 {'0' * 40}\talpha\n"
            f"160000 {index_sha} 1\talpha\n"
            f"160000 {index_sha} 2\talpha\n"
            f"160000 {checkout_sha} 3\talpha\n"
        ),
    )
    assert _git(repo, "submodule", "status", "--recursive").startswith("U")
    after = release_identity._submodules(repo)
    assert len(after["entries"]) == 3
    entry = after["entries"][0]
    assert entry["path"] == "alpha"
    assert entry["index_sha"] == "0" * 40
    assert entry["sha"] == checkout_sha
    assert entry["dirty"] is True
    assert before["digest"] != after["digest"]
    _assert_digest(after)


def test_identity_empty_submodules_has_empty_entries_digest(tmp_path: Path) -> None:
    repo = _init_repo(tmp_path / "repo")
    assert release_identity._submodules(repo) == {
        "entries": [],
        "digest": _expected_digest([]),
    }


def test_snapshot_digest_and_verification_reject_nested_submodule_changes(
    submodule_repo: Path,
) -> None:
    repo = submodule_repo
    authority = repo / "subject.txt"
    manifest = release_identity.create_snapshot(
        repo,
        build_dirs=[],
        authority_paths=[authority],
        commands=[],
    )
    assert manifest["identity_digest"] == _expected_digest(manifest["identity"])
    assert release_identity.verify_snapshot(
        manifest,
        repo,
        build_dirs=[],
        authority_paths=[authority],
    ).valid

    (repo / "beta/nested/extra.txt").write_text("new input\n", encoding="utf-8")
    check = release_identity.verify_snapshot(
        manifest,
        repo,
        build_dirs=[],
        authority_paths=[authority],
    )
    assert not check.valid
    assert "submodules identity mismatch" in check.reasons
    assert manifest["identity_digest"] != _expected_digest(check.current_identity)


def test_identity_rejects_git_failure_in_non_repository(tmp_path: Path) -> None:
    with pytest.raises(
        ValueError, match="cannot read submodule identity.*not a git repository"
    ):
        release_identity._submodules(tmp_path)


@pytest.mark.parametrize("marker", [" ", "-", "+", "U"])
def test_identity_missing_submodule_path_is_conservatively_dirty(
    tmp_path: Path,
    monkeypatch,
    marker: str,
) -> None:
    sha = "a" * 40
    monkeypatch.setattr(
        subprocess,
        "run",
        lambda *args, **kwargs: subprocess.CompletedProcess(
            args[0],
            0,
            stdout=f"{marker}{sha} absent\n",
            stderr="",
        ),
    )
    identity = release_identity._submodules(tmp_path)
    assert identity["entries"] == [
        {
            "path": "absent",
            "index_sha": sha,
            "sha": "missing",
            "dirty": True,
            "tracked_patch": None,
            "untracked": None,
        }
    ]
    _assert_digest(identity)


@pytest.mark.parametrize(
    "output",
    [
        "malformed status\n",
        "\n",
        "a" * 40 + " missing-marker\n",
        " " + "a" * 40 + " duplicate\n " + "a" * 40 + " duplicate\n",
    ],
)
def test_identity_rejects_malformed_or_duplicate_status(
    tmp_path: Path,
    monkeypatch,
    output: str,
) -> None:
    monkeypatch.setattr(
        subprocess,
        "run",
        lambda *args, **kwargs: subprocess.CompletedProcess(
            args[0],
            0,
            stdout=output,
            stderr="",
        ),
    )
    with pytest.raises(ValueError, match="submodule"):
        release_identity._submodules(tmp_path)


@pytest.mark.parametrize(
    "failure",
    [
        OSError("git unavailable"),
        subprocess.TimeoutExpired("git", 10),
        subprocess.CalledProcessError(1, "git", stderr="submodule failed"),
    ],
)
def test_identity_rejects_submodule_command_failure(
    tmp_path: Path,
    monkeypatch,
    failure: Exception,
) -> None:
    def fail(*args, **kwargs):
        raise failure

    monkeypatch.setattr(subprocess, "run", fail)
    with pytest.raises(ValueError, match="submodule"):
        release_identity._submodules(tmp_path)


@pytest.mark.parametrize(
    ("preserve_nul", "stdout", "stderr", "expected"),
    [
        (False, " leading and trailing \n", "", "leading and trailing"),
        (True, " leading\0trailing \0", "", " leading\0trailing \0"),
        (False, "", " error output \n", "error output"),
    ],
)
def test_run_keeps_existing_trim_nul_and_stderr_behavior(
    tmp_path: Path,
    monkeypatch,
    preserve_nul: bool,
    stdout: str,
    stderr: str,
    expected: str,
) -> None:
    monkeypatch.setattr(
        subprocess,
        "run",
        lambda *args, **kwargs: subprocess.CompletedProcess(
            args[0],
            1 if stderr else 0,
            stdout=stdout,
            stderr=stderr,
        ),
    )
    assert (
        release_identity._run(tmp_path, ["git"], preserve_nul=preserve_nul) == expected
    )


@pytest.mark.parametrize(
    "failure",
    [
        OSError("git unavailable"),
        subprocess.TimeoutExpired("git", 10),
    ],
)
def test_run_keeps_existing_unavailable_error_behavior(
    tmp_path: Path,
    monkeypatch,
    failure: Exception,
) -> None:
    def fail(*args, **kwargs):
        raise failure

    monkeypatch.setattr(subprocess, "run", fail)
    assert release_identity._run(tmp_path, ["git"]) == (
        f"<unavailable:{type(failure).__name__}>"
    )


def test_toolchain_records_required_sanitizers_without_probing_tsan(
    tmp_path: Path,
    monkeypatch,
) -> None:
    commands: list[list[str]] = []
    runtimes = {
        "-print-file-name=libasan.so": "/runtime/libasan.so",
        "-print-file-name=libubsan.so": "/runtime/libubsan.so",
        "-print-file-name=liblsan.so": "/runtime/liblsan.so",
    }

    def run(args, **kwargs):
        commands.append(list(args))
        if "-print-file-name=libtsan.so" in args:
            raise AssertionError("non-required TSan must not be probed")
        return subprocess.CompletedProcess(
            args,
            0,
            stdout=runtimes.get(args[-1], "test tool"),
            stderr="",
        )

    monkeypatch.setattr(subprocess, "run", run)
    toolchain = release_identity._toolchain(tmp_path, [])
    assert toolchain["sanitizers"] == {
        "asan": "/runtime/libasan.so",
        "ubsan": "/runtime/libubsan.so",
        "lsan": "/runtime/liblsan.so",
    }
    assert [
        args[-1] for args in commands if args[-1].startswith("-print-file-name=")
    ] == [
        "-print-file-name=libasan.so",
        "-print-file-name=libubsan.so",
        "-print-file-name=liblsan.so",
    ]
