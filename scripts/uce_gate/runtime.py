"""Runtime helpers for scripts/run_uce_gate.py."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
import json
import platform
import shutil
import subprocess
import tarfile
import time

from scripts.uce.manifest import add_result


@dataclass(frozen=True)
class GateResult:
    """Store the result of an executed gate command."""

    gate_id: str
    command: list[str]
    returncode: int
    duration_s: float
    log_path: Path


def utc_now_iso() -> str:
    """Return the current UTC time in ISO-8601 format."""

    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def safe_mkdir(path: Path) -> None:
    """Create a directory tree if needed."""

    path.mkdir(parents=True, exist_ok=True)


def write_text(path: Path, text: str) -> None:
    """Write UTF-8 text to disk."""

    safe_mkdir(path.parent)
    path.write_text(text, encoding="utf-8")


def write_json(path: Path, payload: dict[str, Any]) -> None:
    """Write a JSON document to disk."""

    write_text(path, json.dumps(payload, ensure_ascii=False, indent=2) + "\n")


def run_capture(
    command: list[str],
    *,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    """Run a command and capture stdout/stderr together."""

    return subprocess.run(
        command,
        cwd=str(cwd) if cwd else None,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


def run_gate(
    gate_id: str,
    command: list[str],
    log_path: Path,
    *,
    cwd: Path,
    env: dict[str, str] | None = None,
) -> GateResult:
    """Run a single gate command and persist its output."""

    started = time.time()
    completed = run_capture(command, cwd=cwd, env=env)
    duration_s = round(time.time() - started, 3)
    write_text(log_path, completed.stdout or "")
    return GateResult(
        gate_id=gate_id,
        command=command,
        returncode=int(completed.returncode),
        duration_s=duration_s,
        log_path=log_path,
    )


def collect_versions(repo_root: Path) -> str:
    """Collect a small best-effort environment snapshot."""

    commands = (
        ["uname", "-a"],
        ["git", "rev-parse", "HEAD"],
        ["cmake", "--version"],
        ["ctest", "--version"],
        ["python3", "--version"],
        ["curl", "--version"],
        ["docker", "--version"],
    )
    lines = [f"# generated_at_utc: {utc_now_iso()}\n", f"# platform: {platform.platform()}\n\n"]
    for command in commands:
        try:
            result = run_capture(command, cwd=repo_root)
            lines.append(f"$ {' '.join(command)}\n")
            lines.append(f"returncode: {result.returncode}\n")
            if (result.stdout or "").strip():
                lines.append(result.stdout.rstrip() + "\n")
            lines.append("\n")
        except Exception as exc:  # pragma: no cover - best effort snapshot
            lines.append(f"$ {' '.join(command)}\nexception: {exc}\n\n")
    return "".join(lines)


def parse_shell_exports(env_file: Path) -> dict[str, str]:
    """Parse `export NAME=value` lines from a shell env file."""

    exports: dict[str, str] = {}
    for raw in env_file.read_text(encoding="utf-8", errors="replace").splitlines():
        raw = raw.strip()
        if not raw.startswith("export "):
            continue
        _, payload = raw.split("export ", 1)
        key, _, value = payload.partition("=")
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in {"'", '"'}:
            value = value[1:-1]
        exports[key.strip()] = value
    return exports


def tar_gz_dir(src_dir: Path, out_tgz: Path) -> None:
    """Create a tar.gz archive for the evidence bundle."""

    safe_mkdir(out_tgz.parent)
    with tarfile.open(out_tgz, "w:gz") as tar:
        for path in sorted(src_dir.rglob("*")):
            if path.is_file():
                tar.add(path, arcname=str(path.relative_to(src_dir.parent)))


def best_effort_copytree(src: Path, dst: Path) -> bool:
    """Copy a directory tree when it exists."""

    if not src.exists():
        return False
    safe_mkdir(dst.parent)
    shutil.copytree(src, dst, dirs_exist_ok=True)
    return True


def best_effort_copy_glob(src_root: Path, pattern: str, dst_root: Path) -> int:
    """Copy files matching a glob pattern into the evidence bundle."""

    if not src_root.exists():
        return 0
    copied = 0
    for path in src_root.glob(pattern):
        if not path.is_file():
            continue
        relative = path.relative_to(src_root)
        output = dst_root / relative
        safe_mkdir(output.parent)
        shutil.copy2(path, output)
        copied += 1
    return copied


def resolve_qt_test_binary(build_dir: Path, binary_name: str) -> Path:
    """Resolve a Qt test binary from the build tree."""

    candidates = (
        build_dir / "tests" / binary_name,
        build_dir / binary_name,
    )
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[0]


def record_gate_result(manifest: dict[str, Any], gate: GateResult) -> None:
    """Persist a gate result into the manifest."""

    add_result(
        manifest,
        result_id=gate.gate_id,
        kind="gate",
        result="pass" if gate.returncode == 0 else "fail",
        returncode=gate.returncode,
        duration_s=gate.duration_s,
        log_file=str(gate.log_path),
    )
