"""Default-install package safety contracts for QCurl release gates."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any
from typing import Callable


SCHEMA = "qcurl/package-safety-gate@v2"
DELIVERY_TARGETS = ("Core", "BlockingExtras", "TestSupport", "OtherExtras")
EXPECTED_ARTIFACTS = {
    "Core": "runtime-library",
    "BlockingExtras": "interface-consumer-surface",
    "TestSupport": "development-static-library",
    "OtherExtras": "runtime-library",
}


class PackageGateError(RuntimeError):
    """Report a fail-closed package or release-candidate contract violation."""


@dataclass(frozen=True)
class ReleaseCandidate:
    """Describe release-relevant capability state from a configured build tree."""

    websocket_capable: bool
    websocket_enabled: bool
    force_disabled: bool


def load_contract(path: Path) -> dict[str, Any]:
    """Load a package safety contract JSON object."""

    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise PackageGateError(f"cannot load package gate manifest: {exc}") from exc
    if not isinstance(payload, dict):
        raise PackageGateError("package gate manifest root must be an object")
    return payload


def _require_nonempty_string_list(target: str, field: str, value: Any) -> None:
    if not isinstance(value, list) or not value or not all(
        isinstance(item, str) and item for item in value
    ):
        raise PackageGateError(f"{target}.{field} must be a non-empty string list")


def validate_contract(contract: dict[str, Any]) -> None:
    """Validate the four-target delivery evidence mapping."""

    if contract.get("schema") != SCHEMA:
        raise PackageGateError(f"unsupported package gate schema: {contract.get('schema')!r}")
    if contract.get("installMode") != "unfiltered-all-components":
        raise PackageGateError("package gate must use the unfiltered all-components install")

    targets = contract.get("deliveryTargets")
    if not isinstance(targets, dict) or set(targets) != set(DELIVERY_TARGETS):
        raise PackageGateError(
            "deliveryTargets must contain exactly Core, BlockingExtras, TestSupport and OtherExtras"
        )

    for name in DELIVERY_TARGETS:
        target = targets[name]
        if not isinstance(target, dict):
            raise PackageGateError(f"deliveryTargets.{name} must be an object")
        if target.get("artifact") != EXPECTED_ARTIFACTS[name]:
            raise PackageGateError(f"{name}.artifact does not match the delivery contract")
        if target.get("productionRuntime") is not (name != "TestSupport"):
            raise PackageGateError(f"{name}.productionRuntime does not match the delivery contract")
        cmake_target = target.get("cmakeTarget")
        if not isinstance(cmake_target, str) or not cmake_target.startswith("QCurl::"):
            raise PackageGateError(f"{name}.cmakeTarget must name an exported QCurl target")
        _require_nonempty_string_list(name, "installComponents", target.get("installComponents"))
        _require_nonempty_string_list(name, "lifecycleTests", target.get("lifecycleTests"))
        _require_nonempty_string_list(name, "sanitizerEvidence", target.get("sanitizerEvidence"))
        consumers = target.get("consumerTests")
        if not isinstance(consumers, dict) or set(consumers) != {"shared", "static"}:
            raise PackageGateError(f"{name}.consumerTests must cover shared and static")
        if not all(isinstance(value, str) and value for value in consumers.values()):
            raise PackageGateError(f"{name}.consumerTests values must be non-empty test names")

    policy = contract.get("candidatePolicy")
    if not isinstance(policy, dict) or policy.get("forceDisabledReleaseEligible") is not False:
        raise PackageGateError("force-disabled capability variants must not be release eligible")
    web_socket_tests = policy.get("webSocketTests")
    if web_socket_tests != ["tst_QCWebSocket", "tst_QCWebSocketPool"]:
        raise PackageGateError("candidate policy must require WebSocket and Pool tests")


def _cache_bool(cache_text: str, key: str) -> bool:
    match = re.search(rf"^{re.escape(key)}:BOOL=(?P<value>ON|OFF)$", cache_text, re.MULTILINE)
    if match is None:
        raise PackageGateError(f"configured build is missing {key}")
    return match.group("value") == "ON"


def validate_release_candidate(build_dir: Path) -> ReleaseCandidate:
    """Reject force-disabled builds and report the configured WebSocket capability state."""

    cache_path = build_dir / "CMakeCache.txt"
    config_path = build_dir / "src" / "QCurlConfig.h"
    try:
        cache_text = cache_path.read_text(encoding="utf-8")
        config_text = config_path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as exc:
        raise PackageGateError(f"cannot inspect configured release candidate: {exc}") from exc

    force_disabled = _cache_bool(cache_text, "QCURL_FORCE_DISABLE_WEBSOCKET_SUPPORT")
    if force_disabled:
        raise PackageGateError(
            "QCURL_FORCE_DISABLE_WEBSOCKET_SUPPORT=ON is a negative capability variant, "
            "not a release candidate"
        )

    version_match = re.search(
        r'^\s*#\s*define\s+QCURL_LIBCURL_VERSION\s+"(?P<version>[0-9]+\.[0-9]+\.[0-9]+)',
        config_text,
        re.MULTILINE,
    )
    if version_match is None:
        raise PackageGateError("configured build is missing QCURL_LIBCURL_VERSION")
    version = tuple(int(part) for part in version_match.group("version").split("."))
    websocket_capable = version >= (7, 86, 0)
    websocket_enabled = re.search(
        r"^\s*#\s*define\s+QCURL_WEBSOCKET_SUPPORT(?:\s|$)",
        config_text,
        re.MULTILINE,
    ) is not None
    if websocket_capable and not websocket_enabled:
        raise PackageGateError(
            "WebSocket-capable libcurl must use the default enabled configuration for release"
        )
    return ReleaseCandidate(
        websocket_capable=websocket_capable,
        websocket_enabled=websocket_enabled,
        force_disabled=False,
    )


def _header_owner(header: str, manifests: dict[str, list[str]]) -> str | None:
    owners = [target for target, headers in manifests.items() if header in headers]
    if len(owners) > 1:
        raise PackageGateError(f"installed header has multiple target owners: {header}")
    return owners[0] if owners else None


def _owners_for_path(path: str, manifests: dict[str, list[str]]) -> list[str]:
    name = Path(path).name
    if path.startswith("include/qcurl/"):
        if name == "QCurlConfig.h":
            return ["Core"]
        owner = _header_owner(name, manifests)
        return [owner] if owner else ["Package"]
    if re.search(r"(?:^|/)libQCurlOtherExtras(?:\.|$)", path) or name.startswith(
        "QCurlOtherExtras."
    ):
        return ["OtherExtras"]
    if re.search(r"(?:^|/)libQCurlBlockingExtras(?:\.|$)", path) or name.startswith(
        "QCurlBlockingExtras."
    ):
        return ["BlockingExtras"]
    if re.search(r"(?:^|/)libQCurlTestSupport(?:\.|$)", path) or name.startswith(
        "QCurlTestSupport."
    ):
        return ["TestSupport"]
    if re.search(r"(?:^|/)libQCurl(?:\.|$)", path) or name.startswith("QCurl."):
        return ["Core"]
    if name == "qcurl.pc":
        return ["Core"]
    if name == "qcurl-other-extras.pc":
        return ["OtherExtras"]
    if name.startswith("QCurlBlockingExtrasTargets"):
        return ["BlockingExtras"]
    if name.startswith("QCurlTestSupportTargets"):
        return ["TestSupport"]
    if name.startswith("QCurlOtherExtrasTargets"):
        return ["OtherExtras"]
    if name.startswith("QCurlTargets"):
        return ["Core"]
    return ["Package"]


def build_install_inventory(
    stage_dir: Path,
    manifests: dict[str, list[str]],
) -> dict[str, Any]:
    """Build a relative, exhaustive file inventory for an unfiltered install tree."""

    if set(manifests) != set(DELIVERY_TARGETS):
        raise PackageGateError("header manifests must cover all four delivery targets")
    if not stage_dir.is_dir():
        raise PackageGateError(f"missing unfiltered install tree: {stage_dir}")

    files: list[dict[str, Any]] = []
    target_files = {name: [] for name in DELIVERY_TARGETS}
    for file_path in sorted(path for path in stage_dir.rglob("*") if path.is_file()):
        relative = file_path.relative_to(stage_dir).as_posix()
        if re.search(r"(?:^|/)libQCurlBlockingExtras(?:\.|$)", relative):
            raise PackageGateError(
                "BlockingExtras must not install an independent runtime library: " + relative
            )
        owners = _owners_for_path(relative, manifests)
        files.append({"path": relative, "owners": owners})
        for owner in owners:
            if owner in target_files:
                target_files[owner].append(relative)

    if not files:
        raise PackageGateError("unfiltered install tree is empty")
    missing_targets = [name for name, paths in target_files.items() if not paths]
    if missing_targets:
        raise PackageGateError(
            "unfiltered install tree has no files for targets: " + ", ".join(missing_targets)
        )

    return {
        "schema": "qcurl/package-install-inventory@v1",
        "files": files,
        "deliveryTargets": {
            name: {"files": target_files[name]} for name in DELIVERY_TARGETS
        },
    }


def write_install_inventory(
    stage_dir: Path,
    manifests: dict[str, list[str]],
    output_path: Path,
) -> None:
    """Write the exhaustive default-install inventory JSON."""

    inventory = build_install_inventory(stage_dir, manifests)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(inventory, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


RunCommand = Callable[..., subprocess.CompletedProcess[str]]


def _run_command(command: list[str], *, capture_output: bool) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, text=True, capture_output=capture_output, check=False)


def run_lifecycle_gate(
    build_dir: Path,
    contract: dict[str, Any],
    *,
    ctest: str,
    cmake: str = "cmake",
    jobs: int | None = None,
    report: Path | None = None,
    run_command: RunCommand = _run_command,
) -> int:
    """Run every target lifecycle test required by the package contract."""

    validate_contract(contract)
    candidate = validate_release_candidate(build_dir)
    required = {
        test_name
        for target in contract["deliveryTargets"].values()
        for test_name in target["lifecycleTests"]
    }
    if not candidate.websocket_enabled:
        required.difference_update(contract["candidatePolicy"]["webSocketTests"])

    listing = run_command(
        [ctest, "--test-dir", str(build_dir), "--show-only=json-v1"],
        capture_output=True,
    )
    if listing.returncode != 0:
        raise PackageGateError(
            f"cannot list lifecycle tests: {listing.stderr.strip() or listing.stdout.strip()}"
        )
    try:
        listing_payload = json.loads(listing.stdout)
        registered = {
            test["name"]
            for test in listing_payload["tests"]
            if isinstance(test, dict) and isinstance(test.get("name"), str)
        }
    except (json.JSONDecodeError, KeyError, TypeError) as exc:
        raise PackageGateError("ctest lifecycle listing is not valid JSON v1") from exc

    missing = sorted(required - registered)
    if missing:
        raise PackageGateError("required lifecycle tests are not registered: " + ", ".join(missing))

    build_command = [cmake, "--build", str(build_dir), "--target", *sorted(required)]
    if jobs is not None and jobs > 0:
        build_command.extend(["-j", str(jobs)])
    built = run_command(build_command, capture_output=False)
    if built.returncode != 0:
        return int(built.returncode)

    pattern = "^(" + "|".join(re.escape(name) for name in sorted(required)) + ")$"
    test_command = [
        ctest,
        "--test-dir",
        str(build_dir),
        "--output-on-failure",
        "-R",
        pattern,
    ]
    if report is not None:
        report.parent.mkdir(parents=True, exist_ok=True)
        test_command.extend(("--output-junit", str(report)))
    completed = run_command(
        test_command,
        capture_output=False,
    )
    if completed.returncode == 0 and report is not None:
        if not report.is_file() or report.stat().st_size == 0:
            raise PackageGateError(f"lifecycle report was not produced: {report}")
    return int(completed.returncode)


def _parse_manifest(value: str) -> tuple[str, Path]:
    target, separator, raw_path = value.partition("=")
    if not separator or target not in DELIVERY_TARGETS or not raw_path:
        raise argparse.ArgumentTypeError(
            "manifest must use TARGET=PATH for Core, BlockingExtras, TestSupport or OtherExtras"
        )
    return target, Path(raw_path)


def main(argv: list[str] | None = None) -> int:
    """Run package safety contract checks from the release gate."""

    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    validate_parser = subparsers.add_parser("validate-contract")
    validate_parser.add_argument("manifest", type=Path)

    candidate_parser = subparsers.add_parser("validate-candidate")
    candidate_parser.add_argument("build_dir", type=Path)

    lifecycle_parser = subparsers.add_parser("run-lifecycle-gate")
    lifecycle_parser.add_argument("--build-dir", type=Path, required=True)
    lifecycle_parser.add_argument("--manifest", type=Path, required=True)
    lifecycle_parser.add_argument("--ctest", default="ctest")
    lifecycle_parser.add_argument("--cmake", default="cmake")
    lifecycle_parser.add_argument("--jobs", type=int)
    lifecycle_parser.add_argument("--report", type=Path)

    inventory_parser = subparsers.add_parser("inventory")
    inventory_parser.add_argument("--stage-dir", type=Path, required=True)
    inventory_parser.add_argument("--output", type=Path, required=True)
    inventory_parser.add_argument("--manifest", action="append", required=True, type=_parse_manifest)

    args = parser.parse_args(argv)
    try:
        if args.command == "validate-contract":
            validate_contract(load_contract(args.manifest))
            print("[package_gate] contract passed")
        elif args.command == "validate-candidate":
            candidate = validate_release_candidate(args.build_dir)
            capability = "enabled" if candidate.websocket_enabled else "unavailable"
            print(f"[package_gate] default candidate passed; WebSocket={capability}")
        elif args.command == "run-lifecycle-gate":
            rc = run_lifecycle_gate(
                args.build_dir,
                load_contract(args.manifest),
                ctest=args.ctest,
                cmake=args.cmake,
                jobs=args.jobs,
                report=args.report,
            )
            if rc != 0:
                return rc
            print("[package_gate] four-target delivery lifecycle gate passed")
        else:
            manifests = {target: path.read_text(encoding="utf-8").splitlines() for target, path in args.manifest}
            write_install_inventory(args.stage_dir, manifests, args.output)
            print(f"[package_gate] install inventory written: {args.output}")
    except (PackageGateError, OSError, UnicodeDecodeError) as exc:
        print(f"[package_gate] {exc}", file=__import__("sys").stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
