from __future__ import annotations

import json
import re
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def _source(relative: str) -> str:
    return (REPO_ROOT / relative).read_text(encoding="utf-8")


def _class_body(source: str, class_name: str) -> str:
    match = re.search(
        rf"class\s+{re.escape(class_name)}\b[^{{]*\{{(?P<body>.*?)\n\}};",
        source,
        flags=re.DOTALL,
    )
    assert match is not None, f"未找到 QObject 派生类：{class_name}"
    return match.group("body")


def _function_definition(source: str, qualified_name: str) -> str:
    signature_start = source.find(f"{qualified_name}(")
    assert signature_start >= 0, f"未找到函数定义：{qualified_name}"
    body_start = source.find("{", signature_start)
    assert body_start >= 0, f"未找到函数体：{qualified_name}"

    depth = 0
    for index in range(body_start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[signature_start : index + 1]
    raise AssertionError(f"函数体花括号不平衡：{qualified_name}")


def test_diagnostics_qobject_subclasses_declare_required_macros() -> None:
    targets = {
        "src/private/QCNetworkDiagnosticsOperation_p.h": "DiagnosticsOperation",
        "src/QCNetworkDiagnosticsConnectivity.cpp": "DiagnosisSequence",
        "src/QCNetworkDiagnosticsProcess.cpp": "ProcessDiagnostics",
    }

    for relative, class_name in targets.items():
        body = _class_body(_source(relative), class_name)
        assert re.search(r"^\s*Q_OBJECT\s*$", body, flags=re.MULTILINE), relative
        assert f"Q_DISABLE_COPY_MOVE({class_name})" in body, relative


def test_cookie_destroyed_functor_uses_manager_context_and_direct_delivery() -> None:
    source = _source("src/QCNetworkAccessManagerCookies.cpp")
    pattern = re.compile(
        r"QObject::connect\(\s*manager\s*,\s*&QObject::destroyed\s*,\s*manager\s*,"
        r"\s*\[completion, failure\]\(\)\s*\{.*?\}\s*,\s*Qt::DirectConnection\s*\)",
        flags=re.DOTALL,
    )

    assert pattern.search(source), (
        "cookie manager destroyed functor 必须使用 manager context，并显式指定 "
        "Qt::DirectConnection"
    )


def test_multimanager_reply_entrypoints_are_owner_thread_only() -> None:
    manager_header = _source("src/QCCurlMultiManager.h")
    manager_source = _source("src/QCCurlMultiManager.cpp")

    assert "接受跨线程投递的管理操作" not in manager_header
    assert "@note 线程安全\n" not in manager_header
    assert manager_header.count("@note 线程合同：仅允许在 manager owner thread 调用") == 2
    assert "marshalAddReplyIfNeeded" not in manager_header
    assert "marshalAddReplyIfNeeded" not in manager_source

    for function_name in ("addReply", "removeReply"):
        definition = _function_definition(
            manager_source, f"QCCurlMultiManager::{function_name}"
        )
        thread_guard = definition.find("QThread::currentThread() != thread()")
        null_guard = definition.find("if (!reply)")
        assert thread_guard >= 0, f"{function_name} 缺少 manager owner-thread 门禁"
        assert null_guard >= 0, f"{function_name} 缺少空参数门禁"
        assert thread_guard < null_guard, f"{function_name} 必须在读取 reply 前拒绝错误线程"
        assert "QMetaObject::invokeMethod" not in definition


def test_scheduler_owner_thread_internal_paths_do_not_marshal_qobject_pointers() -> None:
    scheduler_functions = {
        "src/QCNetworkRequestScheduler.cpp": ("startRequest",),
        "src/private/QCNetworkRequestSchedulerFinalize.cpp": (
            "onRequestFinished",
            "onReplyDestroyed",
        ),
    }
    for relative, function_names in scheduler_functions.items():
        source = _source(relative)
        for function_name in function_names:
            definition = _function_definition(
                source, f"QCNetworkRequestScheduler::{function_name}"
            )
            assert "invokeOnSchedulerOwnerThread" not in definition, (
                f"{relative}: {function_name} 不得把 live QObject 排回 owner thread"
            )

            owner_assert = definition.find("Internal::assertSchedulerOwnerThread")
            thread_guard = definition.find("QThread::currentThread() != thread()")
            payload_guard = min(
                index
                for index in (definition.find("if (!reply)"), definition.find("if (!obj)"))
                if index >= 0
            )
            assert 0 <= thread_guard < owner_assert < payload_guard, (
                f"{relative}: {function_name} 必须在读取 QObject 参数前建立 owner-thread invariant"
            )
            pre_owner = definition[:owner_assert]
            assert not re.search(
                r"QPointer<\s*(?:QCNetworkReply|QObject)\s*>", pre_owner
            ), f"{relative}: {function_name} 错误线程分支不得构造 guarded pointer"


def test_scheduler_progress_tracking_uses_auto_connection() -> None:
    source = _source("src/private/QCNetworkRequestSchedulerPrivate.cpp")
    definition = _function_definition(
        source, "QCNetworkRequestScheduler::Impl::connectProgressTracking"
    )

    assert definition.count("Qt::AutoConnection") == 2
    assert "Qt::QueuedConnection" not in definition
    contexts = re.findall(
        r"&QCNetworkReply::(?:downloadProgress|uploadProgress),\s*scheduler,", definition
    )
    assert len(contexts) == 2, "progress functor 必须绑定 scheduler context"


def test_t7_public_contract_headers_publish_doxygen_contract_notes() -> None:
    inventory = json.loads(
        _source("tests/public_api/public_contract_inventory.json")
    )
    markers = {
        "errorLifecycle": "@note 错误生命周期：",
        "qobjectBorrow": "@note QObject 借用合同：",
    }

    missing: list[str] = []
    for contract in inventory["contracts"]:
        category = contract["category"]
        if contract["phase"] != "T7" or category not in markers:
            continue
        header = contract["header"]
        if markers[category] not in _source(f"src/{header}"):
            missing.append(f"{contract['id']} -> {header}")

    assert not missing, "缺少普通 Doxygen/Doxyqml 共同语法合同注释：\n" + "\n".join(missing)
