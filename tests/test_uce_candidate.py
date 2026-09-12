from __future__ import annotations

from pathlib import Path
import subprocess

import pytest

from scripts.release_identity import AUTHORITY_RELATIVE_PATHS
from scripts.uce_gate.candidate import capture_candidate_fingerprint


def _git(repo: Path, *arguments: str) -> bytes:
    result = subprocess.run(
        ["git", *arguments], cwd=repo, check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    assert result.returncode == 0, result.stderr.decode("utf-8", errors="replace")
    return result.stdout


def _repository(path: Path) -> Path:
    path.mkdir()
    _git(path, "init", "-q")
    _git(path, "config", "user.name", "UCE Fixture")
    _git(path, "config", "user.email", "uce@example.invalid")
    _git(path, "config", "core.fileMode", "true")
    (path / "tracked.txt").write_text("initial\n", encoding="utf-8")
    _git(path, "add", "tracked.txt")
    _git(path, "commit", "-qm", "fixture initial")
    return path


def test_candidate_changes_when_untracked_executable_mode_changes(tmp_path: Path) -> None:
    repo = _repository(tmp_path / "repo")
    script = repo / "untracked.sh"
    script.write_text("exit 0\n", encoding="utf-8")
    script.chmod(0o644)
    before = capture_candidate_fingerprint(repo, authority_paths=())

    script.chmod(0o755)
    after = capture_candidate_fingerprint(repo, authority_paths=())

    assert before["combined"] != after["combined"]
    assert before["untracked"]["sha256"] != after["untracked"]["sha256"]


def test_candidate_index_identifies_complete_entries_not_only_staged_diff(tmp_path: Path) -> None:
    repo = _repository(tmp_path / "repo")
    before = capture_candidate_fingerprint(repo, authority_paths=())
    (repo / "tracked.txt").write_text("new committed content\n", encoding="utf-8")
    _git(repo, "add", "tracked.txt")
    _git(repo, "commit", "-qm", "fixture content update")

    after = capture_candidate_fingerprint(repo, authority_paths=())

    assert before["staged"] == after["staged"]
    assert before["index"]["sha256"] != after["index"]["sha256"]
    assert after["index"]["entries"][0]["path"] == "tracked.txt"
    assert after["index"]["entries"][0]["mode"] == "100644"


def test_candidate_distinguishes_staged_and_unstaged_binary_changes(tmp_path: Path) -> None:
    repo = _repository(tmp_path / "repo")
    initial = capture_candidate_fingerprint(repo, authority_paths=())
    (repo / "tracked.txt").write_bytes(b"\x00staged\xff")
    _git(repo, "add", "tracked.txt")
    staged = capture_candidate_fingerprint(repo, authority_paths=())

    (repo / "tracked.txt").write_bytes(b"\x00unstaged\xff")
    unstaged = capture_candidate_fingerprint(repo, authority_paths=())

    assert initial["index"]["sha256"] != staged["index"]["sha256"]
    assert initial["staged"]["sha256"] != staged["staged"]["sha256"]
    assert initial["unstaged"] == staged["unstaged"]
    assert staged["index"] == unstaged["index"]
    assert staged["staged"] == unstaged["staged"]
    assert staged["unstaged"]["sha256"] != unstaged["unstaged"]["sha256"]
    assert len({state["combined"] for state in (initial, staged, unstaged)}) == 3


def test_candidate_changes_when_untracked_content_changes(tmp_path: Path) -> None:
    repo = _repository(tmp_path / "repo")
    untracked = repo / "untracked.bin"
    untracked.write_bytes(b"\x00first")
    before = capture_candidate_fingerprint(repo, authority_paths=())

    untracked.write_bytes(b"\x00other")
    after = capture_candidate_fingerprint(repo, authority_paths=())

    assert before["combined"] != after["combined"]
    assert before["untracked"]["sha256"] != after["untracked"]["sha256"]


def _create_authority_files(repo: Path) -> None:
    (repo / ".gitignore").write_text(
        ".helloagents/\n" + "\n".join(AUTHORITY_RELATIVE_PATHS) + "\n", encoding="utf-8"
    )
    for relative in AUTHORITY_RELATIVE_PATHS:
        path = repo / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("authority version one\n", encoding="utf-8")


def test_candidate_detects_ignored_release_authority_content_changes(tmp_path: Path) -> None:
    repo = _repository(tmp_path / "repo")
    _create_authority_files(repo)
    before = capture_candidate_fingerprint(repo)

    (repo / AUTHORITY_RELATIVE_PATHS[0]).write_text("authority version two\n", encoding="utf-8")
    after = capture_candidate_fingerprint(repo)

    assert before["untracked"] == after["untracked"]
    assert before["combined"] != after["combined"]
    assert before["authority"]["sha256"] != after["authority"]["sha256"]
    assert {entry["path"] for entry in after["authority"]["files"]} == set(AUTHORITY_RELATIVE_PATHS)


def test_candidate_rejects_missing_required_release_authority(tmp_path: Path) -> None:
    repo = _repository(tmp_path / "repo")

    with pytest.raises(RuntimeError, match="权威文件"):
        capture_candidate_fingerprint(repo)


def _add_submodule(repo: Path, source: Path, relative: str) -> Path:
    _git(repo, "-c", "protocol.file.allow=always", "submodule", "add", "-q", str(source), relative)
    _git(repo, "commit", "-qam", "fixture submodule")
    return repo / relative


@pytest.mark.parametrize("change", ["worktree", "index", "untracked"])
def test_candidate_tracks_content_changes_inside_already_dirty_submodules(
    tmp_path: Path, change: str
) -> None:
    source = _repository(tmp_path / "submodule-source")
    repo = _repository(tmp_path / "repo")
    child = _add_submodule(repo, source, "child module")
    changed = child / ("new.txt" if change == "untracked" else "tracked.txt")
    changed.write_text("dirty one\n", encoding="utf-8")
    if change == "index":
        _git(child, "add", "tracked.txt")
    before = capture_candidate_fingerprint(repo, authority_paths=())

    changed.write_text("dirty two\n", encoding="utf-8")
    if change == "index":
        _git(child, "add", "tracked.txt")
    after = capture_candidate_fingerprint(repo, authority_paths=())

    assert before["head"] == after["head"]
    assert before["unstaged"] == after["unstaged"]
    assert before["combined"] != after["combined"]
    assert before["submodules"]["sha256"] != after["submodules"]["sha256"]


def test_candidate_rejects_uninitialized_gitlink(tmp_path: Path) -> None:
    repo = _repository(tmp_path / "repo")
    object_id = _git(repo, "rev-parse", "HEAD").decode("ascii").strip()
    _git(repo, "update-index", "--add", "--cacheinfo", f"160000,{object_id},missing-module")

    with pytest.raises(RuntimeError, match="子模块"):
        capture_candidate_fingerprint(repo, authority_paths=())


def test_candidate_recurses_into_nested_gitlinks(tmp_path: Path) -> None:
    leaf_source = _repository(tmp_path / "leaf-source")
    child_source = _repository(tmp_path / "child-source")
    _add_submodule(child_source, leaf_source, "leaf")
    repo = _repository(tmp_path / "repo")
    child = _add_submodule(repo, child_source, "child")
    _git(repo, "-c", "protocol.file.allow=always", "submodule", "update", "--init", "--recursive")
    changed = child / "leaf/tracked.txt"
    changed.write_text("nested dirty one\n", encoding="utf-8")
    before = capture_candidate_fingerprint(repo, authority_paths=())

    changed.write_text("nested dirty two\n", encoding="utf-8")
    after = capture_candidate_fingerprint(repo, authority_paths=())

    assert before["head"] == after["head"]
    assert before["combined"] != after["combined"]
    before_child = before["submodules"]["entries"][0]["fingerprint"]
    after_child = after["submodules"]["entries"][0]["fingerprint"]
    assert before_child["head"] == after_child["head"]
    assert before_child["submodules"]["sha256"] != after_child["submodules"]["sha256"]


def test_candidate_excludes_own_evidence_and_ignored_build_outputs(tmp_path: Path) -> None:
    repo = _repository(tmp_path / "repo")
    (repo / ".gitignore").write_text("build*/\n", encoding="utf-8")
    outputs = (repo / "evidence/run", repo / "evidence/run.tar.gz", repo / "evidence/run.envelope.json")
    before = capture_candidate_fingerprint(repo, authority_paths=(), excluded_paths=outputs)
    outputs[0].mkdir(parents=True)
    (outputs[0] / "candidate_fingerprint.json").write_text("generated\n", encoding="utf-8")
    outputs[1].write_bytes(b"generated archive")
    outputs[2].write_text("generated envelope\n", encoding="utf-8")
    (repo / "build-run").mkdir()
    (repo / "build-run/binary").write_bytes(b"ignored executable")

    after = capture_candidate_fingerprint(repo, authority_paths=(), excluded_paths=outputs)

    assert before["combined"] == after["combined"]
    assert before["untracked"] == after["untracked"]


def test_candidate_excludes_child_outputs_without_hiding_child_source_changes(tmp_path: Path) -> None:
    source = _repository(tmp_path / "submodule-source")
    repo = _repository(tmp_path / "repo")
    child = _add_submodule(repo, source, "child")
    output = child / "generated/evidence"
    before = capture_candidate_fingerprint(repo, authority_paths=(), excluded_paths=(output,))
    output.mkdir(parents=True)
    (output / "manifest.json").write_text("generated\n", encoding="utf-8")

    after_output = capture_candidate_fingerprint(repo, authority_paths=(), excluded_paths=(output,))
    (child / "tracked.txt").write_text("source update\n", encoding="utf-8")
    after_source = capture_candidate_fingerprint(repo, authority_paths=(), excluded_paths=(output,))

    assert before["combined"] == after_output["combined"]
    assert after_output["combined"] != after_source["combined"]


def test_candidate_tracks_checked_out_submodule_head(tmp_path: Path) -> None:
    source = _repository(tmp_path / "submodule-source")
    repo = _repository(tmp_path / "repo")
    child = _add_submodule(repo, source, "child")
    before = capture_candidate_fingerprint(repo, authority_paths=())
    _git(
        child, "-c", "user.name=UCE Fixture", "-c", "user.email=uce@example.invalid",
        "commit", "--allow-empty", "-qm", "fixture child head",
    )

    after = capture_candidate_fingerprint(repo, authority_paths=())

    assert before["index"] == after["index"]
    before_child = before["submodules"]["entries"][0]["fingerprint"]
    after_child = after["submodules"]["entries"][0]["fingerprint"]
    assert before_child["index"] == after_child["index"]
    assert before_child["head"] != after_child["head"]
    assert before["combined"] != after["combined"]


def test_candidate_hashes_authority_bytes_behind_ignored_symlink(tmp_path: Path) -> None:
    repo = _repository(tmp_path / "repo")
    _create_authority_files(repo)
    content = tmp_path / "authority-content"
    content.write_text("authority one\n", encoding="utf-8")
    authority = repo / AUTHORITY_RELATIVE_PATHS[0]
    authority.unlink()
    authority.symlink_to(content)
    before = capture_candidate_fingerprint(repo)

    content.write_text("authority two\n", encoding="utf-8")
    after = capture_candidate_fingerprint(repo)

    assert before["untracked"] == after["untracked"]
    assert before["authority"]["sha256"] != after["authority"]["sha256"]
    assert before["combined"] != after["combined"]


def test_candidate_rejects_excluding_the_entire_repository(tmp_path: Path) -> None:
    repo = _repository(tmp_path / "repo")

    with pytest.raises(ValueError, match="整个仓库"):
        capture_candidate_fingerprint(repo, authority_paths=(), excluded_paths=(repo,))
