"""QCurl release gate 的六棵物理构建树模型。"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any


TREE_IDS = (
    "release-shared",
    "release-static",
    "test-shared-gcc",
    "test-shared-clang",
    "asan-ubsan-lsan",
    "tsan",
)

TREE_ARGUMENTS = {
    "release-shared": "release_shared_build_dir",
    "release-static": "release_static_build_dir",
    "test-shared-gcc": "test_shared_gcc_build_dir",
    "test-shared-clang": "test_shared_clang_build_dir",
    "asan-ubsan-lsan": "asan_ubsan_lsan_build_dir",
    "tsan": "tsan_build_dir",
}


@dataclass(frozen=True)
class TreeSpec:
    """描述一棵 release gate 构建树的固定能力。"""

    tree_id: str
    build_testing: str
    shared_libs: str
    compiler_family: str
    sanitizer_profile: str | None = None


TREE_SPECS = (
    TreeSpec("release-shared", "OFF", "ON", "gcc"),
    TreeSpec("release-static", "OFF", "OFF", "gcc"),
    TreeSpec("test-shared-gcc", "ON", "ON", "gcc"),
    TreeSpec("test-shared-clang", "ON", "ON", "clang"),
    TreeSpec("asan-ubsan-lsan", "ON", "ON", "clang", "asan-ubsan-lsan"),
    TreeSpec("tsan", "ON", "ON", "clang", "tsan"),
)
TREE_SPEC_BY_ID = {spec.tree_id: spec for spec in TREE_SPECS}


def tree_path(args: Any, tree_id: str) -> Path:
    """返回已解析的 tree 路径；缺少路径时立即失败。"""

    try:
        argument = TREE_ARGUMENTS[tree_id]
    except KeyError as exc:
        raise ValueError(f"unknown producer tree: {tree_id}") from exc
    value = getattr(args, argument, None)
    if value is None:
        raise ValueError(f"release gate requires explicit tree path: {tree_id}")
    return Path(value)


def tree_registry(args: Any) -> dict[str, dict[str, Any]]:
    """构建不含 fallback 的固定 tree registry。"""

    registry: dict[str, dict[str, Any]] = {}
    seen: dict[Path, str] = {}
    for spec in TREE_SPECS:
        argument = TREE_ARGUMENTS[spec.tree_id]
        value = getattr(args, argument, None)
        if value is None:
            continue
        path = Path(value).resolve()
        previous = seen.get(path)
        if previous is not None:
            raise ValueError(f"producer trees must be physically distinct: {previous} and {spec.tree_id}")
        seen[path] = spec.tree_id
        registry[spec.tree_id] = {
            "tree_id": spec.tree_id,
            "path": path,
            "build_testing": spec.build_testing,
            "shared_libs": spec.shared_libs,
            "compiler_family": spec.compiler_family,
            "sanitizer_profile": spec.sanitizer_profile,
        }
    return registry


def required_tree_ids(tier: str) -> tuple[str, ...]:
    """返回指定 gate tier 允许消费的显式 producer tree 集合。"""

    if tier == "fast":
        return ("release-shared",)
    if tier == "strict":
        return ("release-shared", "test-shared-gcc")
    if tier == "full":
        return TREE_IDS
    raise ValueError(f"unsupported release gate tier: {tier}")


def compiler_family(value: str | None) -> str:
    """根据 CMake 编译器路径归一化 GCC/Clang 家族。"""

    if not value:
        return "missing"
    name = Path(value).name.lower()
    if "clang" in name:
        return "clang"
    if name in {"gcc", "g++", "c++", "cc", "cxx"} or "gcc" in name:
        return "gcc"
    return "unknown"


def sanitizer_matches(options: dict[str, str], profile: str | None) -> bool:
    """检查 sanitizer tree 是否记录了对应 profile。"""

    if profile is None:
        return True
    values = " ".join(options.values()).lower()
    if profile == "asan-ubsan-lsan":
        return all(token in values for token in ("asan", "ubsan", "lsan"))
    if profile == "tsan":
        return "tsan" in values
    return False
