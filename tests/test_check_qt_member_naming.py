from __future__ import annotations

import json
from pathlib import Path
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


def test_clang_query_timeout_is_reported_as_ast_scan_error(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    source_path = tmp_path / "src/Foo.cpp"
    _write_source(tmp_path, "src/Foo.cpp", "struct Foo { int m_value = 0; };\n")
    database_path = _write_compile_database(tmp_path, source_path)

    def timeout(*args: object, **kwargs: object) -> None:
        assert kwargs["timeout"] == 180
        raise subprocess.TimeoutExpired(cmd=args[0], timeout=180)

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
