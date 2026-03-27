#!/usr/bin/env python3
"""Detect netproof provider capabilities for UCE tiers."""

from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import subprocess
from datetime import datetime
from datetime import timezone
from pathlib import Path
from typing import Any


TIER_REQUIREMENTS: dict[str, tuple[str, ...]] = {
    "pr": (),
    "nightly": ("strace",),
    "soak": ("strace",),
}

CAPABILITY_BITS = {
    "CAP_NET_ADMIN": 12,
    "CAP_SYS_ADMIN": 21,
}


def _utc_now_iso() -> str:
    """Return the current UTC time in ISO-8601 format."""

    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _read_effective_capabilities() -> set[str]:
    """Read Linux effective capabilities from /proc/self/status."""

    status_path = Path("/proc/self/status")
    if not status_path.exists():
        return set()

    cap_eff_hex = ""
    for line in status_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("CapEff:"):
            cap_eff_hex = line.split(":", 1)[1].strip()
            break
    if not cap_eff_hex:
        return set()

    try:
        cap_eff = int(cap_eff_hex, 16)
    except ValueError:
        return set()

    enabled: set[str] = set()
    for name, bit in CAPABILITY_BITS.items():
        if cap_eff & (1 << bit):
            enabled.add(name)
    return enabled


def _run_probe(command: list[str]) -> tuple[bool, int | None, str | None]:
    """Run a short probe command."""

    try:
        completed = subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=2,
            check=False,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        return False, None, str(exc)

    if completed.returncode == 0:
        return True, completed.returncode, None

    details = (completed.stderr or completed.stdout or "").strip()
    return False, completed.returncode, details or f"probe failed with rc={completed.returncode}"


def _provider_record(
    *,
    name: str,
    binary: str | None,
    probe_command: list[str],
    capability: str,
    available: bool,
    downgrade_reason: str | None,
    required_privileges: list[str],
    covers: list[str],
    not_covered: list[str],
    probe_rc: int | None,
) -> dict[str, Any]:
    """Build a provider record."""

    return {
        "available": available,
        "capability": capability,
        "binary": binary,
        "probe_command": probe_command,
        "probe_returncode": probe_rc,
        "downgrade_reason": downgrade_reason,
        "required_privileges": required_privileges,
        "covers": covers,
        "not_covered": not_covered,
    }


def _detect_strace(system_name: str) -> dict[str, Any]:
    """Detect strace availability."""

    probe_command = ["strace", "-V"]
    if system_name != "Linux":
        return _provider_record(
            name="strace",
            binary=None,
            probe_command=probe_command,
            capability="unsupported_platform",
            available=False,
            downgrade_reason="netproof 当前只定义 Linux 平台能力探测。",
            required_privileges=[],
            covers=["进程级网络 syscall 观察（connect/sendmsg/recvmsg 等）"],
            not_covered=["流量整形", "network namespace 隔离"],
            probe_rc=None,
        )

    binary = shutil.which("strace")
    if not binary:
        return _provider_record(
            name="strace",
            binary=None,
            probe_command=probe_command,
            capability="missing_binary",
            available=False,
            downgrade_reason="未在 PATH 中找到 strace。",
            required_privileges=[],
            covers=["进程级网络 syscall 观察（connect/sendmsg/recvmsg 等）"],
            not_covered=["流量整形", "network namespace 隔离"],
            probe_rc=None,
        )

    ok, probe_rc, reason = _run_probe([binary, "-V"])
    return _provider_record(
        name="strace",
        binary=binary,
        probe_command=[binary, "-V"],
        capability="available" if ok else "probe_failed",
        available=ok,
        downgrade_reason=reason,
        required_privileges=[],
        covers=["进程级网络 syscall 观察（connect/sendmsg/recvmsg 等）"],
        not_covered=["流量整形", "network namespace 隔离"],
        probe_rc=probe_rc,
    )


def _detect_tc(system_name: str, enabled_caps: set[str]) -> dict[str, Any]:
    """Detect tc availability."""

    probe_command = ["tc", "-V"]
    if system_name != "Linux":
        return _provider_record(
            name="tc",
            binary=None,
            probe_command=probe_command,
            capability="unsupported_platform",
            available=False,
            downgrade_reason="tc 仅在 Linux/iproute2 平台定义。",
            required_privileges=["CAP_NET_ADMIN"],
            covers=["qdisc-based 延迟/丢包/限速 shaping"],
            not_covered=["syscall 观察", "namespace 创建"],
            probe_rc=None,
        )

    binary = shutil.which("tc")
    if not binary:
        return _provider_record(
            name="tc",
            binary=None,
            probe_command=probe_command,
            capability="missing_binary",
            available=False,
            downgrade_reason="未在 PATH 中找到 tc。",
            required_privileges=["CAP_NET_ADMIN"],
            covers=["qdisc-based 延迟/丢包/限速 shaping"],
            not_covered=["syscall 观察", "namespace 创建"],
            probe_rc=None,
        )

    ok, probe_rc, reason = _run_probe([binary, "-V"])
    if not ok:
        return _provider_record(
            name="tc",
            binary=binary,
            probe_command=[binary, "-V"],
            capability="probe_failed",
            available=False,
            downgrade_reason=reason,
            required_privileges=["CAP_NET_ADMIN"],
            covers=["qdisc-based 延迟/丢包/限速 shaping"],
            not_covered=["syscall 观察", "namespace 创建"],
            probe_rc=probe_rc,
        )

    if os.geteuid() != 0 and "CAP_NET_ADMIN" not in enabled_caps:
        return _provider_record(
            name="tc",
            binary=binary,
            probe_command=[binary, "-V"],
            capability="insufficient_privilege",
            available=False,
            downgrade_reason="tc 已安装，但当前进程缺少 CAP_NET_ADMIN，无法稳定执行 qdisc 变更。",
            required_privileges=["CAP_NET_ADMIN"],
            covers=["qdisc-based 延迟/丢包/限速 shaping"],
            not_covered=["syscall 观察", "namespace 创建"],
            probe_rc=probe_rc,
        )

    return _provider_record(
        name="tc",
        binary=binary,
        probe_command=[binary, "-V"],
        capability="available",
        available=True,
        downgrade_reason=None,
        required_privileges=["CAP_NET_ADMIN"],
        covers=["qdisc-based 延迟/丢包/限速 shaping"],
        not_covered=["syscall 观察", "namespace 创建"],
        probe_rc=probe_rc,
    )


def _detect_netns(system_name: str, enabled_caps: set[str]) -> dict[str, Any]:
    """Detect ip netns availability."""

    probe_command = ["ip", "netns", "list"]
    if system_name != "Linux":
        return _provider_record(
            name="netns",
            binary=None,
            probe_command=probe_command,
            capability="unsupported_platform",
            available=False,
            downgrade_reason="network namespace 探测仅对 Linux 定义。",
            required_privileges=["CAP_SYS_ADMIN"],
            covers=["namespace 级 loopback / 路由隔离"],
            not_covered=["流量整形", "syscall 观察"],
            probe_rc=None,
        )

    binary = shutil.which("ip")
    if not binary:
        return _provider_record(
            name="netns",
            binary=None,
            probe_command=probe_command,
            capability="missing_binary",
            available=False,
            downgrade_reason="未在 PATH 中找到 ip（iproute2）。",
            required_privileges=["CAP_SYS_ADMIN"],
            covers=["namespace 级 loopback / 路由隔离"],
            not_covered=["流量整形", "syscall 观察"],
            probe_rc=None,
        )

    ok, probe_rc, reason = _run_probe([binary, "netns", "list"])
    if not ok:
        return _provider_record(
            name="netns",
            binary=binary,
            probe_command=[binary, "netns", "list"],
            capability="probe_failed",
            available=False,
            downgrade_reason=reason,
            required_privileges=["CAP_SYS_ADMIN"],
            covers=["namespace 级 loopback / 路由隔离"],
            not_covered=["流量整形", "syscall 观察"],
            probe_rc=probe_rc,
        )

    if os.geteuid() != 0 and "CAP_SYS_ADMIN" not in enabled_caps:
        return _provider_record(
            name="netns",
            binary=binary,
            probe_command=[binary, "netns", "list"],
            capability="insufficient_privilege",
            available=False,
            downgrade_reason="ip netns 可执行，但当前进程缺少 CAP_SYS_ADMIN，无法稳定创建/管理 namespace。",
            required_privileges=["CAP_SYS_ADMIN"],
            covers=["namespace 级 loopback / 路由隔离"],
            not_covered=["流量整形", "syscall 观察"],
            probe_rc=probe_rc,
        )

    return _provider_record(
        name="netns",
        binary=binary,
        probe_command=[binary, "netns", "list"],
        capability="available",
        available=True,
        downgrade_reason=None,
        required_privileges=["CAP_SYS_ADMIN"],
        covers=["namespace 级 loopback / 路由隔离"],
        not_covered=["流量整形", "syscall 观察"],
        probe_rc=probe_rc,
    )


def build_tier_summary(tier: str, providers: dict[str, dict[str, Any]]) -> dict[str, Any]:
    """Build a tier-level capability summary."""

    required = list(TIER_REQUIREMENTS[tier])
    missing_required = [name for name in required if not providers[name]["available"]]
    optional_missing = [
        name
        for name, payload in providers.items()
        if name not in required and not payload["available"]
    ]

    if missing_required:
        capability = "missing_required"
    elif optional_missing:
        capability = "degraded"
    else:
        capability = "full"

    downgrade_reasons = [
        providers[name]["downgrade_reason"]
        for name in [*missing_required, *optional_missing]
        if providers[name]["downgrade_reason"]
    ]
    return {
        "capability": capability,
        "required_providers": required,
        "missing_required_providers": missing_required,
        "optional_missing_providers": optional_missing,
        "downgrade_reasons": downgrade_reasons,
    }


def build_report(selected_tier: str = "all") -> dict[str, Any]:
    """Build the full netproof capability report."""

    system_name = platform.system()
    enabled_caps = _read_effective_capabilities()
    providers = {
        "strace": _detect_strace(system_name),
        "tc": _detect_tc(system_name, enabled_caps),
        "netns": _detect_netns(system_name, enabled_caps),
    }

    tiers = {}
    for tier in ("pr", "nightly", "soak"):
        if selected_tier != "all" and selected_tier != tier:
            continue
        tiers[tier] = build_tier_summary(tier, providers)

    return {
        "schema_version": 1,
        "provider": "netproof",
        "generated_at_utc": _utc_now_iso(),
        "platform": {
            "system": system_name,
            "release": platform.release(),
            "euid": os.geteuid(),
            "effective_capabilities": sorted(enabled_caps),
        },
        "providers": providers,
        "tiers": tiers,
    }


def main(argv: list[str] | None = None) -> int:
    """CLI entry point."""

    parser = argparse.ArgumentParser(description="Detect netproof provider capabilities for UCE tiers.")
    parser.add_argument(
        "--tier",
        choices=("all", "pr", "nightly", "soak"),
        default="all",
        help="Only emit the selected tier summary (default: all).",
    )
    parser.add_argument(
        "--output",
        help="Optional JSON output path. Default: print to stdout.",
    )
    parser.add_argument(
        "--compact",
        action="store_true",
        help="Emit compact JSON instead of pretty-printed JSON.",
    )
    args = parser.parse_args(argv)

    report = build_report(selected_tier=args.tier)
    json_text = json.dumps(
        report,
        ensure_ascii=False,
        indent=None if args.compact else 2,
        separators=(",", ":") if args.compact else None,
    )

    if args.output:
        output_path = Path(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json_text + "\n", encoding="utf-8")
    else:
        print(json_text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
