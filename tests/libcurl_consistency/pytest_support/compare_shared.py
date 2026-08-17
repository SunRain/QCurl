"""一致性工件比较器的通用字段辅助函数。"""

from __future__ import annotations

from typing import Dict, List, Mapping, Tuple


def compare_dict(lhs: Dict, rhs: Dict, fields: List[str]) -> List[str]:
    """比较两个映射中的指定字段。"""

    diffs = []
    for field in fields:
        if lhs.get(field) != rhs.get(field):
            diffs.append(f"{field} mismatch: {lhs.get(field)} != {rhs.get(field)}")
    return diffs


def compare_list_dict(
    lhs_list: List[Dict],
    rhs_list: List[Dict],
    fields: List[str],
    label: str,
) -> List[str]:
    """逐项比较两个映射列表中的指定字段。"""

    diffs: List[str] = []
    if len(lhs_list) != len(rhs_list):
        diffs.append(f"{label} length mismatch: {len(lhs_list)} != {len(rhs_list)}")
        return diffs
    for index, (lhs, rhs) in enumerate(zip(lhs_list, rhs_list)):
        for field in fields:
            if lhs.get(field) != rhs.get(field):
                diffs.append(
                    f"{label}[{index}].{field} mismatch: "
                    f"{lhs.get(field)} != {rhs.get(field)}"
                )
    return diffs


def compare_optional_fields(
    lhs: Dict,
    rhs: Dict,
    fields: List[str],
    prefix: str,
) -> List[str]:
    """比较两侧同时存在的可选字段，并拒绝单侧缺失。"""

    diffs: List[str] = []
    for field in fields:
        lhs_has = field in lhs
        rhs_has = field in rhs
        if lhs_has != rhs_has:
            diffs.append(f"{prefix}.{field} missing in one side")
            continue
        if lhs_has and rhs_has and lhs.get(field) != rhs.get(field):
            diffs.append(
                f"{prefix}.{field} mismatch: {lhs.get(field)} != {rhs.get(field)}"
            )
    return diffs


def value_at_path(payload: Dict, path: str) -> tuple[bool, object]:
    """按点分隔路径读取嵌套字段。"""

    current: object = payload
    for component in path.split("."):
        if not isinstance(current, dict) or component not in current:
            return False, None
        current = current[component]
    return True, current


def validate_required_fields(
    payload: Dict,
    *,
    side: str,
    required_fields: Mapping[str, type | tuple[type, ...]],
) -> List[str]:
    """验证一侧工件的必需字段存在且类型正确。"""

    diffs: List[str] = []
    for path, expected_type in required_fields.items():
        present, value = value_at_path(payload, path)
        if not present:
            diffs.append(f"{side} {path} missing")
            continue
        if not isinstance(value, expected_type):
            diffs.append(
                f"{side} {path} has invalid type: "
                f"{type(value).__name__}, expected {expected_type}"
            )
    return diffs


def extract_error_namespaces(payload: Dict) -> Tuple[Dict, Dict]:
    """提取当前 schema 的 observed/derived 错误命名空间。"""

    observed = payload.get("observed") if isinstance(payload.get("observed"), dict) else {}
    derived = payload.get("derived") if isinstance(payload.get("derived"), dict) else {}
    observed_error = observed.get("error") if isinstance(observed.get("error"), dict) else {}
    derived_error = derived.get("error") if isinstance(derived.get("error"), dict) else {}
    return observed_error, derived_error
