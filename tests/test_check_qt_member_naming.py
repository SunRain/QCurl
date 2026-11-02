from __future__ import annotations

import json
from pathlib import Path
import re
import subprocess

import pytest

from scripts import check_qt_member_naming as naming
from scripts import qt_member_ast as ast


def _write_source(root: Path, relative_path: str, source: str) -> None:
    path = root / relative_path
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(source, encoding="utf-8")


def _write_compile_database(root: Path, source_path: Path) -> Path:
    build_path = root / "build"
    build_path.mkdir()
    database_path = build_path / "compile_commands.json"
    database_path.write_text(
        json.dumps(
            [
                {
                    "directory": str(root),
                    "file": str(source_path),
                    "arguments": ["c++", "-c", str(source_path)],
                }
            ]
        ),
        encoding="utf-8",
    )
    return database_path


def test_public_member_scan_ignores_method_locals_and_private_state() -> None:
    body = """
public:
    void update()
    {
        int m_local = 0;
        (void)m_local;
    }

private:
    int m_state = 0;
"""

    assert naming.public_member_prefixes(body) == []


def test_public_member_scan_keeps_braced_field_initializers() -> None:
    body = """
public:
    QSet<int> m_values{1, 2};
"""

    assert naming.public_member_prefixes(body) == ["m_values"]


def test_record_scan_uses_struct_default_public_access() -> None:
    assert naming.public_member_prefixes(
        "int m_value = 0;", default_access="public"
    ) == ["m_value"]


@pytest.mark.parametrize("visibility", ["", "Q_DECL_HIDDEN "])
def test_unlisted_pimpl_type_is_rejected(
    tmp_path: Path, capsys, visibility: str
) -> None:
    _write_source(
        tmp_path,
        "src/Foo.cpp",
        f"class {visibility}FooPrivate {{ public: int state = 0; }};\n",
    )

    assert naming.main(["--source-root", str(tmp_path)]) == 1
    assert "missing explicit authorization" in capsys.readouterr().err


@pytest.mark.parametrize("visibility", ["", "Q_DECL_HIDDEN "])
def test_shared_data_authorization_requires_a_real_holder(
    tmp_path: Path, visibility: str
) -> None:
    _write_source(
        tmp_path,
        "src/Foo.cpp",
        f"class {visibility}FooData : public QSharedData {{ public: int value = 0; }};\n",
    )
    authorization = naming.DirectFieldAuthorization(
        relative_path="src/Foo.cpp",
        type_name="FooData",
        kind="shared_data",
    )

    violations = naming.validate_authorizations(tmp_path, (authorization,))

    assert any("shared-data holder" in violation for violation in violations)


def test_hidden_pimpl_preserves_authorization_and_member_checks(tmp_path: Path) -> None:
    source = """
/// 保存 Foo 的私有状态。
class Q_DECL_HIDDEN FooPrivate
{
    Q_DECLARE_PUBLIC(Foo)
public:
    int value = 0;
};
"""
    authorization = naming.DirectFieldAuthorization(
        relative_path="src/Foo_p.h",
        type_name="FooPrivate",
        kind="pimpl",
        public_type="Foo",
    )
    _write_source(tmp_path, authorization.relative_path, source)

    assert naming.validate_authorizations(tmp_path, (authorization,)) == []
    assert naming.inspect_authorized_types(tmp_path, (authorization,)) == []

    _write_source(
        tmp_path, authorization.relative_path, source.replace("value", "m_value")
    )
    assert naming.inspect_authorized_types(tmp_path, (authorization,)) == [
        "src/Foo_p.h:FooPrivate: public 直接字段不得使用 m_: m_value"
    ]


@pytest.mark.parametrize("declaration", ["class", "struct"])
def test_hidden_record_keeps_comment_and_default_access_checks(
    tmp_path: Path, declaration: str
) -> None:
    _write_source(
        tmp_path,
        "src/Foo.cpp",
        f"/// 保存一次操作的数据。\n{declaration} Q_DECL_HIDDEN Snapshot {{ int m_value; }};\n",
    )
    authorization = naming.DirectFieldAuthorization(
        relative_path="src/Foo.cpp", type_name="Snapshot", kind="record"
    )

    assert naming.validate_authorizations(tmp_path, (authorization,)) == []
    expected = (
        ["src/Foo.cpp:Snapshot: public 直接字段不得使用 m_: m_value"]
        if declaration == "struct"
        else []
    )
    assert naming.inspect_authorized_types(tmp_path, (authorization,)) == expected


def test_shared_data_rejects_owner_specific_back_pointer(tmp_path: Path) -> None:
    _write_source(
        tmp_path,
        "src/Foo.cpp",
        """
class FooData : public QSharedData
{
public:
    Foo *q_ptr = nullptr;
};
class Foo { QSharedDataPointer<FooData> d; };
""",
    )
    authorization = naming.DirectFieldAuthorization(
        relative_path="src/Foo.cpp",
        type_name="FooData",
        kind="shared_data",
    )

    violations = naming.validate_authorizations(tmp_path, (authorization,))

    assert any("owner 专属 q_ptr" in violation for violation in violations)


def test_ast_result_maps_nested_member_to_innermost_record(tmp_path: Path) -> None:
    source_path = tmp_path / "src/Foo.cpp"
    _write_source(
        tmp_path,
        "src/Foo.cpp",
        """
class SchedulerQueues
{
public:
    struct QueuedRequest
    {
        int m_key = 0;
    };

    int m_count = 0;
};
""",
    )
    output = f"""
{source_path}:7:13: note: \"field\" binds here
{source_path}:10:9: note: \"field\" binds here
"""

    members = naming.parse_ast_public_members(tmp_path, output)

    assert [(member.record_name, member.field_name) for member in members] == [
        ("QueuedRequest", "m_key"),
        ("SchedulerQueues", "m_count"),
    ]


def test_unclassified_public_member_is_rejected() -> None:
    member = naming.PublicPrefixedMember(
        relative_path="src/Foo.cpp",
        record_name="SchedulerQueues",
        field_name="m_count",
        line=7,
    )

    violations = naming.classify_public_members((member,), ())

    assert violations == [
        "src/Foo.cpp:7:SchedulerQueues: public m_ 字段未分类: m_count; "
        "普通行为类必须封装，直接字段类型必须显式授权"
    ]


def test_authorized_type_is_not_reported_as_unclassified() -> None:
    member = naming.PublicPrefixedMember(
        relative_path="src/Foo.cpp",
        record_name="Record",
        field_name="m_value",
        line=4,
    )
    authorization = naming.DirectFieldAuthorization(
        relative_path="src/Foo.cpp",
        type_name="Record",
        kind="record",
    )

    assert naming.classify_public_members((member,), (authorization,)) == []


def test_clang_query_compile_arguments_drop_gcc_only_flags() -> None:
    arguments = [
        "/usr/bin/c++",
        "-std=c++17",
        "-mno-direct-extern-access",
        "-c",
        "src/Foo.cpp",
    ]

    assert naming.sanitize_compile_arguments(arguments) == [
        "/usr/bin/c++",
        "-std=c++17",
        "-c",
        "src/Foo.cpp",
    ]


@pytest.mark.parametrize(("file_count", "variant_count"), [(8, 2), (1, 17)])
def test_public_member_scan_bounds_actual_compilations_and_keeps_variants(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    file_count: int,
    variant_count: int,
) -> None:
    sources: dict[str, str] = {}
    entries = []
    for index in range(file_count):
        relative_path = f"src/F{index}.cpp"
        source = f"struct Foo{index}\n{{\n    int m_common;\n"
        for variant in range(variant_count):
            source += f"#if VARIANT == {variant}\n    int m_variant{variant};\n#endif\n"
        source += "};\n"
        _write_source(tmp_path, relative_path, source)
        sources[relative_path] = source
        for variant in range(variant_count):
            entries.append(
                {
                    "directory": str(tmp_path),
                    "file": relative_path,
                    "arguments": [
                        "c++",
                        "-std=c++17",
                        f"-DVARIANT={variant}",
                        "-c",
                        relative_path,
                    ],
                }
            )
    database_path = _write_compile_database(tmp_path, tmp_path / "src/F0.cpp")
    database_path.write_text(json.dumps(entries), encoding="utf-8")

    compilations: list[int] = []
    real_run = ast.subprocess.run

    def observe_run(
        command: list[str], **kwargs: object
    ) -> subprocess.CompletedProcess[str]:
        result = real_run(command, **kwargs)
        # 每个真实 AST 都有且仅有一个 m_common，不能只点数文件参数。
        compilations.append(
            len(
                re.findall(
                    r"^FieldDecl\b[^\n]*\bm_common\b", result.stdout, re.MULTILINE
                )
            )
        )
        return result

    monkeypatch.setattr(ast.subprocess, "run", observe_run)

    members = naming.discover_public_members(
        tmp_path, sources, compile_commands=database_path
    )

    assert max(compilations) <= 8
    assert sum(compilations) == file_count * variant_count
    assert {
        (member.relative_path, member.record_name, member.field_name)
        for member in members
    } == {
        (f"src/F{index}.cpp", f"Foo{index}", name)
        for index in range(file_count)
        for name in (
            "m_common",
            *(f"m_variant{variant}" for variant in range(variant_count)),
        )
    }


def test_public_member_scan_keeps_native_header_command_inference(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    source_path = tmp_path / "src/Foo.cpp"
    _write_source(tmp_path, "src/Foo.cpp", '#include "Foo.h"\n')
    source = """#ifndef ENABLE_FIELD
#error missing inferred compile flags
#endif
struct Foo
{
    int m_value;
};
"""
    _write_source(tmp_path, "src/Foo.h", source)
    database_path = _write_compile_database(tmp_path, source_path)
    database_path.write_text(
        json.dumps(
            [
                {
                    "directory": str(tmp_path),
                    "file": str(source_path),
                    "arguments": ["c++", "-DENABLE_FIELD", "-c", str(source_path)],
                }
            ]
            * 17
        ),
        encoding="utf-8",
    )

    compilations: list[int] = []
    real_run = ast.subprocess.run

    def observe_run(
        command: list[str], **kwargs: object
    ) -> subprocess.CompletedProcess[str]:
        result = real_run(command, **kwargs)
        compilations.append(len(ast.FIELD_DUMP_PATTERN.findall(result.stdout)))
        return result

    monkeypatch.setattr(ast.subprocess, "run", observe_run)

    members = naming.discover_public_members(
        tmp_path, {"src/Foo.h": source}, compile_commands=database_path
    )

    # donor 有 17 条命令，但原生头文件推断只加载一个 AST。
    assert compilations == [1]
    assert [
        (member.relative_path, member.record_name, member.field_name)
        for member in members
    ] == [("src/Foo.h", "Foo", "m_value")]


def test_public_member_scan_rejects_unmapped_macro_binding(tmp_path: Path) -> None:
    _write_source(tmp_path, "include/field.h", "#define PUBLIC_FIELD int m_value;\n")
    source = """#include "../include/field.h"
struct Foo
{
    PUBLIC_FIELD
};
void use(Foo& foo) { (void)foo.m_value; }
"""
    _write_source(tmp_path, "src/Foo.cpp", source)
    database_path = _write_compile_database(tmp_path, tmp_path / "src/Foo.cpp")

    with pytest.raises(naming.AstScanError, match="匹配数量与可分类字段数量不一致"):
        naming.discover_public_members(
            tmp_path, {"src/Foo.cpp": source}, compile_commands=database_path
        )


@pytest.mark.parametrize(
    ("binding_count", "stderr"),
    [(0, ""), (1, ""), (2, ""), (2, "Processing file Foo.cpp.\n")],
)
def test_public_member_scan_validates_raw_bindings_before_deduplication(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    binding_count: int,
    stderr: str,
) -> None:
    source_path = tmp_path / "src/Foo.cpp"
    source = "struct Foo\n{\n    int m_value;\n};\n"
    _write_source(tmp_path, "src/Foo.cpp", source)
    database_path = _write_compile_database(tmp_path, source_path)
    stdout = (
        f"FieldDecl 0x1 <{source_path}:3:5, col:9> col:9 m_value 'int'\n"
        * binding_count
    )
    stdout += f"{binding_count} {'match' if binding_count == 1 else 'matches'}.\n"

    monkeypatch.setattr(
        ast.subprocess,
        "run",
        lambda command, **kwargs: subprocess.CompletedProcess(
            command, 0, stdout, stderr
        ),
    )
    members = naming.discover_public_members(
        tmp_path,
        {"src/Foo.cpp": source},
        compile_commands=database_path,
        clang_query="/bin/true",
    )

    assert [member.field_name for member in members] == (
        ["m_value"] if binding_count else []
    )


@pytest.mark.parametrize("stdout", ["", "1 match.\n", "2 matches.\n"])
def test_public_member_scan_rejects_missing_or_unmapped_match_summary(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, stdout: str
) -> None:
    source_path = tmp_path / "src/Foo.cpp"
    source = "struct Foo\n{\n    int m_value;\n};\n"
    _write_source(tmp_path, "src/Foo.cpp", source)
    database_path = _write_compile_database(tmp_path, source_path)
    monkeypatch.setattr(
        ast.subprocess,
        "run",
        lambda command, **kwargs: subprocess.CompletedProcess(
            command, 0, stdout, "Processing file Foo.cpp.\n"
        ),
    )

    with pytest.raises(naming.AstScanError):
        naming.discover_public_members(
            tmp_path,
            {"src/Foo.cpp": source},
            compile_commands=database_path,
            clang_query="/bin/true",
        )


@pytest.mark.parametrize("failure", ["timeout", "start", "exit", "diagnostic", "count"])
def test_public_member_scan_aborts_after_later_compile_batch_failure(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, failure: str
) -> None:
    source_path = tmp_path / "src/Foo.cpp"
    source = "struct Foo\n{\n    int m_value;\n};\n"
    _write_source(tmp_path, "src/Foo.cpp", source)
    database_path = _write_compile_database(tmp_path, source_path)
    entry = json.loads(database_path.read_text(encoding="utf-8"))[0]
    database_path.write_text(json.dumps([entry] * 17), encoding="utf-8")
    calls: list[Path] = []

    def fail_later(
        command: list[str], **kwargs: object
    ) -> subprocess.CompletedProcess[str]:
        calls.append(Path(command[2]))
        if len(calls) == 2:
            if failure == "timeout":
                raise subprocess.TimeoutExpired(command, 180)
            if failure == "start":
                raise OSError("injected start failure")
            if failure == "exit":
                return subprocess.CompletedProcess(command, -9, "", "")
            if failure == "diagnostic":
                return subprocess.CompletedProcess(
                    command, 0, "", "error: injected parse failure\n"
                )
            return subprocess.CompletedProcess(command, 0, "1 match.\n", "")
        stdout = (
            f"FieldDecl 0x1 <{source_path}:3:5, col:9> col:9 m_value 'int'\n1 match.\n"
        )
        return subprocess.CompletedProcess(command, 0, stdout, "")

    monkeypatch.setattr(ast.subprocess, "run", fail_later)
    with pytest.raises(naming.AstScanError):
        naming.discover_public_members(
            tmp_path,
            {"src/Foo.cpp": source},
            compile_commands=database_path,
            clang_query="/bin/true",
        )

    assert len(calls) == 2
    assert all(not directory.exists() for directory in calls)


def test_clang_query_timeout_is_reported_as_ast_scan_error(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    source_path = tmp_path / "src/Foo.cpp"
    _write_source(tmp_path, "src/Foo.cpp", "struct Foo { int m_value = 0; };\n")
    database_path = _write_compile_database(tmp_path, source_path)

    def timeout(*args: object, **kwargs: object) -> None:
        assert kwargs["timeout"] == 180
        raise subprocess.TimeoutExpired(cmd=str(args[0]), timeout=180)

    monkeypatch.setattr(ast.subprocess, "run", timeout)

    with pytest.raises(naming.AstScanError, match="clang-query 执行超时.*180"):
        naming.discover_public_members(
            tmp_path,
            {"src/Foo.cpp": source_path.read_text(encoding="utf-8")},
            compile_commands=database_path,
            clang_query="/bin/true",
        )


def test_clang_query_start_failure_is_reported_as_ast_scan_error(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    source_path = tmp_path / "src/Foo.cpp"
    _write_source(tmp_path, "src/Foo.cpp", "struct Foo { int m_value = 0; };\n")
    database_path = _write_compile_database(tmp_path, source_path)

    def fail_to_start(*args: object, **kwargs: object) -> None:
        raise OSError("permission denied")

    monkeypatch.setattr(ast.subprocess, "run", fail_to_start)

    with pytest.raises(naming.AstScanError, match="无法启动 clang-query: permission denied"):
        naming.discover_public_members(
            tmp_path,
            {"src/Foo.cpp": source_path.read_text(encoding="utf-8")},
            compile_commands=database_path,
            clang_query="/bin/true",
        )


def test_invalid_compile_database_is_reported_as_ast_scan_error(
    tmp_path: Path,
) -> None:
    source_path = tmp_path / "src/Foo.cpp"
    _write_source(tmp_path, "src/Foo.cpp", "struct Foo { int m_value = 0; };\n")
    build_path = tmp_path / "build"
    build_path.mkdir()
    database_path = build_path / "compile_commands.json"
    database_path.write_text("{invalid", encoding="utf-8")

    with pytest.raises(
        naming.AstScanError, match="无法解析 compile_commands.json"
    ):
        naming.discover_public_members(
            tmp_path,
            {"src/Foo.cpp": source_path.read_text(encoding="utf-8")},
            compile_commands=database_path,
            clang_query="/bin/true",
        )


def test_project_manifest_passes_against_target_commit() -> None:
    assert naming.main(["--source-root", "."]) == 0
