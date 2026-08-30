"""QCurl Linux ELF 动态符号 allowlist producer。"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path
from typing import Collection

if __package__:
    from .qcurl_abi_common import AbiGateError
    from .qcurl_abi_common import resolve_existing_dir
    from .qcurl_abi_common import resolve_existing_file
    from .qcurl_abi_common import run
    from .qcurl_abi_common import tool_path
else:
    from qcurl_abi_common import AbiGateError
    from qcurl_abi_common import resolve_existing_dir
    from qcurl_abi_common import resolve_existing_file
    from qcurl_abi_common import run
    from qcurl_abi_common import tool_path


FORBIDDEN_DYNAMIC_SYMBOL_TOKENS = (
    "QCCurlHandleManager",
    "QCCurlMultiTransferRecord",
    "QCNetworkReplyPrivate",
    "QCurl::QCNetworkReply::deleteLater()",
    "jitterFraction",
    "equalJitterDelay",
)
INTERNAL_COMPONENT_BRIDGE_OWNERS = {
    "OtherExtras": frozenset(
        {
            "registerPersistentTransfer",
            "removePersistentTransfer",
        }
    ),
    "TestSupport": frozenset({"installNetworkMockProvider"}),
}
CORE_DYNAMIC_SUPPORT_OWNERS = frozenset({"operator", "staticMetaObject"})
_EXPORTED_TYPE_RE = re.compile(
    r"\b(?:class|struct)\s+(?P<macro>QCURL_EXPORT|QCURL_BLOCKING_EXTRAS_EXPORT|"
    r"QCURL_OTHER_EXTRAS_EXPORT)\s+"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\b"
)


def _demangled_symbol(line: str) -> str:
    if "|" in line:
        return line.split("|", 1)[1].strip()
    match = re.match(r"^\S+\s+[A-Za-z?]\s+(?P<name>.+)$", line.strip())
    return match.group("name").strip() if match else line.strip()


def _first_party_owner(symbol: str) -> str | None:
    wrappers = (
        "construction vtable for ",
        "covariant return thunk to ",
        "guard variable for ",
        "non-virtual thunk to ",
        "typeinfo for ",
        "typeinfo name for ",
        "virtual thunk to ",
        "vtable for ",
    )
    normalized = symbol
    for wrapper in wrappers:
        if normalized.startswith(wrapper):
            normalized = normalized[len(wrapper) :]
            break
    match = re.match(r"QCurl::(?P<owner>[A-Za-z_][A-Za-z0-9_]*)", normalized)
    return match.group("owner") if match else None


def validate_dynamic_symbol_contract(
    symbols: str,
    *,
    allowed_owners: Collection[str] | None = None,
) -> None:
    """拒绝 private 或不在 allowlist 中的第一方动态符号。"""

    violations: list[str] = []
    allowed = set(allowed_owners) if allowed_owners is not None else None
    for raw_line in symbols.splitlines():
        if not raw_line.strip():
            continue
        demangled = _demangled_symbol(raw_line)
        owner = _first_party_owner(demangled)
        forbidden = any(token in demangled for token in FORBIDDEN_DYNAMIC_SYMBOL_TOKENS)
        outside = allowed is not None and owner is not None and owner not in allowed
        if forbidden or outside:
            violations.append(raw_line.strip())
    if violations:
        raise AbiGateError(
            "private or non-allowlisted first-party symbol leaked into dynamic symbols:\n"
            + "\n".join(violations)
        )


COMPONENT_SPECS = {
    "core": ("Core", ("QCURL_EXPORT", "QCURL_BLOCKING_EXTRAS_EXPORT")),
    "other-extras": ("OtherExtras", ("QCURL_OTHER_EXTRAS_EXPORT",)),
}


def _manifest_headers(manifest: Path, component: str) -> list[str]:
    try:
        path = resolve_existing_file(manifest, "public surface manifest")
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError) as exc:
        raise AbiGateError(f"invalid public surface manifest: {manifest}: {exc}") from exc
    manifest_component, _ = COMPONENT_SPECS[component]
    manifest_components = {manifest_component}
    if component == "core":
        manifest_components.add("BlockingExtras")
    headers = [
        item["path"]
        for item in payload.get("headers", [])
        if isinstance(item, dict)
        and item.get("component") in manifest_components
        and item.get("visibility") == "Public"
        and isinstance(item.get("path"), str)
    ]
    if not headers:
        raise AbiGateError(f"surface manifest contains no {component} headers")
    return sorted(set(headers))


def _exported_free_function_names(source: str, macro: str) -> set[str]:
    names: set[str] = set()
    for statement in source.split(";"):
        if macro not in statement or "(" not in statement:
            continue
        match = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*$", statement.rsplit("(", 1)[0])
        if match:
            names.add(match.group(1))
    return names


def public_symbol_owners(
    manifest: Path,
    source_root: Path,
    *,
    component: str,
) -> set[str]:
    """从 public surface manifest 读取允许导出的符号 owner。"""

    _, macros = COMPONENT_SPECS[component]
    root = resolve_existing_dir(source_root, "source root")
    owners: set[str] = set()
    for relative in _manifest_headers(manifest, component):
        header = resolve_existing_file(root / relative, f"{component} public header")
        source = header.read_text(encoding="utf-8")
        owners.update(
            match.group("name")
            for match in _EXPORTED_TYPE_RE.finditer(source)
            if match.group("macro") in macros
        )
        for macro in macros:
            owners.update(_exported_free_function_names(source, macro))
    if component == "core":
        for bridge_owners in INTERNAL_COMPONENT_BRIDGE_OWNERS.values():
            owners.update(bridge_owners)
        owners.update(CORE_DYNAMIC_SUPPORT_OWNERS)
    return owners


def collect_dynamic_symbols(library: Path) -> str:
    """读取并 demangle 动态库定义的全部符号。"""

    raw = run(
        [tool_path("nm"), "-D", "--defined-only", "--format=posix", str(library)]
    ).stdout
    names = [line.split(None, 1)[0] for line in raw.splitlines() if line.strip()]
    if not names:
        raise AbiGateError(f"no defined dynamic symbols found: {library}")
    proc = subprocess.run(
        [tool_path("c++filt")],
        input="\n".join(names) + "\n",
        text=True,
        capture_output=True,
    )
    if proc.returncode != 0:
        raise AbiGateError(f"c++filt failed: {proc.stderr}")
    demangled = proc.stdout.splitlines()
    if len(demangled) != len(names):
        raise AbiGateError("nm/c++filt symbol count mismatch")
    return "\n".join(
        f"{raw_name}|{display}" for raw_name, display in zip(names, demangled)
    ) + "\n"


def validate_library_dynamic_symbols(
    args: argparse.Namespace,
    library: Path,
) -> None:
    """验证动态符号 allowlist 并写机器 JSON 报告。"""

    symbols = collect_dynamic_symbols(library)
    owners = public_symbol_owners(
        args.surface_manifest,
        args.source_root,
        component=args.component,
    )
    entries = []
    for line in symbols.splitlines():
        mangled, demangled = line.split("|", 1)
        entries.append(
            {
                "mangled": mangled,
                "demangled": demangled,
                "firstPartyOwner": _first_party_owner(demangled),
            }
        )
    report = args.symbol_report.resolve()
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(
        json.dumps(
            {
                "schema": "qcurl-dynamic-symbol-allowlist@v1",
                "component": args.component,
                "library": str(library),
                "allowedOwners": sorted(owners),
                "symbols": entries,
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )
    validate_dynamic_symbol_contract(symbols, allowed_owners=owners)
