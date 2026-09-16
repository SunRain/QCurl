"""验证 policy_violations 字典门禁的静态扫描能力。

回归背景（两次真实缺陷）：

1. 扫描器曾假设 `add_policy_violation("code")`，而 UCE 侧真实签名是
   `add_policy_violation(manifest, code)`。迁移到 UCE 时切换扫描目标却未同步签名，
   门禁退化为采集 0 个 code。
2. 扫描目标长期不含 `httpbin.py` / `run_uce_sanitizers.py` / `gate_report.py`，
   导致 `env_preflight_httpbin_stop_failed`、`sanitizer_*`、`execution_contract` 等
   code 长期未注册而门禁全绿。

因此本测试同时锁定「采集形态」与「目标覆盖」两个维度。
"""

from __future__ import annotations

import json
from pathlib import Path

from scripts.validate_policy_violations_dictionary import _CODE_ACCUMULATOR_NAMES
from scripts.validate_policy_violations_dictionary import _CODE_CARRYING_CALLS
from scripts.validate_policy_violations_dictionary import _CODE_CONSTANT_NAMES
from scripts.validate_policy_violations_dictionary import _scan_codes_from_python_file
from scripts.validate_policy_violations_dictionary import _scan_targets
from scripts.validate_policy_violations_dictionary import count_dynamic_policy_violation_calls

_REPO_ROOT = Path(__file__).resolve().parent.parent

# 所有产出 policy code 的模块。新增产出点时必须同步此清单与门禁的 targets。
_CODE_PRODUCING_MODULES = (
    "scripts/uce_gate/planner.py",
    "scripts/uce_gate/orchestrator.py",
    "scripts/uce_gate/evidence.py",
    "scripts/uce_gate/execute.py",
    "scripts/uce_gate/ctest_gates.py",
    "scripts/uce_gate/finalize.py",
    "scripts/uce_gate/dci_contract.py",
    "scripts/uce_gate/httpbin.py",
    "scripts/uce_gate/qt_contracts.py",
    "scripts/netproof_strace_gate.py",
    "scripts/run_uce_sanitizers.py",
    "tests/libcurl_consistency/pytest_support/gate_report.py",
    "tests/uce/bp/validate.py",
    "tests/uce/ctbp/validate.py",
    "tests/uce/hes/validate.py",
    "tests/uce/timeline/parser.py",
    "tests/uce/timeline/validate.py",
)


def _registered_codes() -> set[str]:
    payload = json.loads(
        (_REPO_ROOT / "docs" / "dev" / "uce" / "policy_violations_dictionary.json").read_text(
            encoding="utf-8"
        )
    )
    return set(payload["entries"])


def _write_probe(tmp_path: Path, source: str) -> Path:
    probe = tmp_path / "probe.py"
    probe.write_text(source, encoding="utf-8")
    return probe


# --- 采集形态 1：调用 -------------------------------------------------------


def test_scanner_detects_direct_call_literal(tmp_path: Path) -> None:
    """`add_policy_violation(manifest, "code")` 的第二个位置参数必须被采集。"""

    probe = _write_probe(
        tmp_path,
        'def f(manifest):\n    add_policy_violation(manifest, "direct_code")\n',
    )

    assert _scan_codes_from_python_file(probe) == {"direct_code"}


def test_scanner_detects_keyword_call_literal(tmp_path: Path) -> None:
    """`code=` 关键字形式同样必须被采集。"""

    probe = _write_probe(
        tmp_path,
        'def f(manifest):\n    add_policy_violation(manifest, code="keyword_code")\n',
    )

    assert _scan_codes_from_python_file(probe) == {"keyword_code"}


def test_scanner_detects_registered_wrapper_literal(tmp_path: Path) -> None:
    """登记在 _CODE_CARRYING_CALLS 中的包装函数不得让 code 变为不可见。"""

    probe = _write_probe(
        tmp_path,
        'def f(manifest):\n    _record_envelope_failure(manifest, "wrapper_code", "msg")\n',
    )

    assert _scan_codes_from_python_file(probe) == {"wrapper_code"}


# --- 采集形态 2：累积器 -----------------------------------------------------


def test_scanner_detects_list_accumulator(tmp_path: Path) -> None:
    """`violations.append("code")` 形态必须被采集。"""

    probe = _write_probe(
        tmp_path,
        'def f():\n    violations = []\n    violations.append("appended_code")\n',
    )

    assert _scan_codes_from_python_file(probe) == {"appended_code"}


def test_scanner_detects_set_accumulator(tmp_path: Path) -> None:
    """`violations.add("code")` 与集合字面量初始化必须被采集。"""

    probe = _write_probe(
        tmp_path,
        'def f():\n    violations = {"seed_code"}\n    violations.add("added_code")\n',
    )

    assert _scan_codes_from_python_file(probe) == {"seed_code", "added_code"}


def test_scanner_detects_annotated_accumulator(tmp_path: Path) -> None:
    """带类型注解的累积器初始化同样必须被采集。"""

    probe = _write_probe(
        tmp_path,
        'def f():\n    policy_violations: list[str] = ["annotated_code"]\n',
    )

    assert _scan_codes_from_python_file(probe) == {"annotated_code"}


def test_scanner_detects_extend_literal_list(tmp_path: Path) -> None:
    """`violations.extend([...])` 中的字面量必须逐项采集。"""

    probe = _write_probe(
        tmp_path,
        'def f():\n    violations = []\n    violations.extend(["a_code", "b_code"])\n',
    )

    assert _scan_codes_from_python_file(probe) == {"a_code", "b_code"}


def test_scanner_detects_policy_field_accumulator(tmp_path: Path) -> None:
    """`report[\"policy_violations\"].append(...)` 也必须被采集。"""

    probe = _write_probe(
        tmp_path,
        'def f(report):\n    report["policy_violations"].append("field_code")\n',
    )

    assert _scan_codes_from_python_file(probe) == {"field_code"}


def test_scanner_detects_registered_policy_constant(tmp_path: Path) -> None:
    """被登记的 policy 常量赋值不得绕过静态扫描。"""

    probe = _write_probe(
        tmp_path,
        'TIMELINE_PARSE_POLICY = "constant_code"\n',
    )

    assert _scan_codes_from_python_file(probe) == {"constant_code"}


def test_scanner_detects_gate_spec_policy_code(tmp_path: Path) -> None:
    """GateSpec 的 policy_code 字段是稳定 code 入口，必须被采集。"""

    probe = _write_probe(
        tmp_path,
        'GateSpec("id", "kind", "selector", "gate_code")\n',
    )

    assert _scan_codes_from_python_file(probe) == {"gate_code"}


def test_scanner_ignores_unregistered_accumulator_name(tmp_path: Path) -> None:
    """未登记的变量名不参与采集，避免把无关字符串误判为 code。"""

    probe = _write_probe(
        tmp_path,
        'def f():\n    messages = []\n    messages.append("not_a_policy_code")\n',
    )

    assert _scan_codes_from_python_file(probe) == set()


# --- 动态调用报告 -----------------------------------------------------------


def test_scanner_reports_variable_code_as_dynamic(tmp_path: Path) -> None:
    """变量实参无法静态解析，必须计入动态调用而不是静默跳过。"""

    probe = _write_probe(
        tmp_path,
        "def f(manifest, code):\n    add_policy_violation(manifest, code)\n",
    )

    assert _scan_codes_from_python_file(probe) == set()
    assert count_dynamic_policy_violation_calls(probe) == [2]


def test_wrapper_body_forwarding_is_not_reported_as_dynamic(tmp_path: Path) -> None:
    """包装函数体内的转发调用不是覆盖缺口，不得计入动态报告。

    `_record_envelope_failure(manifest, code, msg)` 内部的
    `add_policy_violation(manifest, code)` 中 code 是形参，其字面量在调用点已被采集。
    """

    probe = _write_probe(
        tmp_path,
        "def _record_envelope_failure(manifest, code, message):\n"
        "    add_policy_violation(manifest, code)\n",
    )

    assert count_dynamic_policy_violation_calls(probe) == []


# --- 登记表与目标覆盖 -------------------------------------------------------


def test_wrapper_registry_covers_evidence_helper() -> None:
    """evidence.py 的包装函数必须在登记表中，否则其 code 会重新变为不可见。"""

    assert _CODE_CARRYING_CALLS["_record_envelope_failure"] == 1


def test_accumulator_registry_covers_known_names() -> None:
    """三个实际在用的累积器变量名必须都在登记表中。"""

    assert {"policy_violations", "violations", "policy_codes"} <= _CODE_ACCUMULATOR_NAMES


def test_policy_constant_registry_covers_timeline_parser() -> None:
    """timeline parser 的稳定 policy 常量必须在登记表中。"""

    assert "TIMELINE_PARSE_POLICY" in _CODE_CONSTANT_NAMES


def test_scan_targets_match_producer_manifest() -> None:
    """生产扫描目标与显式 producer 清单必须保持一一对应。"""

    actual = tuple(
        target.path.relative_to(_REPO_ROOT).as_posix()
        for target in _scan_targets(_REPO_ROOT)
    )
    assert actual == _CODE_PRODUCING_MODULES
    assert "tests/libcurl_consistency/run_gate.py" not in actual


def test_run_gate_is_a_non_producer_entrypoint() -> None:
    """run_gate.py 只委托 gate 执行，不应被伪列为 policy code producer。"""

    assert _scan_codes_from_python_file(_REPO_ROOT / "tests/libcurl_consistency/run_gate.py") == set()


def test_every_code_producing_module_is_scannable() -> None:
    """每个产出 policy code 的模块都必须能被扫描器采集到至少一个 code。

    采集为空说明该模块换用了未登记的形态，其新增 code 将不受门禁保护。
    """

    for relative in _CODE_PRODUCING_MODULES:
        module = _REPO_ROOT / relative
        assert module.exists(), f"扫描目标不存在: {relative}"
        assert _scan_codes_from_python_file(module), f"未采集到任何 code: {relative}"


def test_all_produced_codes_are_registered() -> None:
    """全部产出模块的字面量 code 必须已登记在字典中。"""

    scanned: set[str] = set()
    for relative in _CODE_PRODUCING_MODULES:
        scanned |= _scan_codes_from_python_file(_REPO_ROOT / relative)

    assert scanned, "扫描器未采集到任何 code —— 签名或登记表可能再次漂移"
    assert scanned <= _registered_codes()


def test_archive_envelope_codes_are_visible_to_scanner() -> None:
    """三个 envelope 失败 code 必须对门禁静态可见（回归防护）。"""

    scanned = _scan_codes_from_python_file(_REPO_ROOT / "scripts" / "uce_gate" / "evidence.py")

    assert {
        "archive_envelope_source_missing",
        "archive_envelope_read_failed",
        "archive_envelope_write_failed",
    } <= scanned
