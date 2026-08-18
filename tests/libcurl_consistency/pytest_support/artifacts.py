"""一致性 artifacts 路径、字段构造与写入工具。"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
from typing import Any, Dict, Iterable, Optional, Tuple


ARTIFACTS_SCHEMA = "qcurl-lc/artifacts@v1"
_SENSITIVE_COMMAND_OPTIONS = frozenset({"--key-pass", "--pass", "--password", "--proxy-pass"})


def redact_command_args(args: Iterable[str]) -> list[str]:
    """保留命令结构并隐藏敏感选项的值。"""

    redacted: list[str] = []
    hide_next = False
    for value in args:
        if hide_next:
            redacted.append("<REDACTED>")
            hide_next = False
            continue
        if value in _SENSITIVE_COMMAND_OPTIONS:
            redacted.append(value)
            hide_next = True
            continue
        option, separator, _ = value.partition("=")
        if separator and option in _SENSITIVE_COMMAND_OPTIONS:
            redacted.append(f"{option}=<REDACTED>")
            continue
        redacted.append(value)
    return redacted

def apply_error_namespaces(payload: Dict[str, Any],
                           *,
                           kind: str,
                           http_status: int,
                           curlcode: Optional[int] = None,
                           http_code: Optional[int] = None) -> None:
    """
    为 artifacts 增加 error 的 observed/derived 命名空间。

    口径：
    - observed：来自服务端/协议栈可直接观测的数据（如 http_status/http_code）
    - derived：由测试/对照逻辑推导或归一化的数据（如 kind/curlcode）
    """
    observed: Dict[str, Any] = payload.get("observed") if isinstance(payload.get("observed"), dict) else {}
    derived: Dict[str, Any] = payload.get("derived") if isinstance(payload.get("derived"), dict) else {}
    observed["error"] = {
        "http_status": int(http_status),
        **({"http_code": int(http_code)} if http_code is not None else {}),
    }
    derived["error"] = {
        "kind": str(kind),
        **({"curlcode": int(curlcode)} if curlcode is not None else {}),
    }
    payload["observed"] = observed
    payload["derived"] = derived


def artifacts_root(env) -> Path:
    """返回当前 gate 隔离目录；手工运行仍使用 testenv 的生成目录。"""

    scoped_root = os.environ.get("QCURL_LC_ARTIFACTS_DIR", "").strip()
    if scoped_root:
        return Path(scoped_root).expanduser().resolve()
    return Path(env.gen_dir) / "artifacts"


def ensure_case_dir(root: Path, suite: str, case: str) -> Path:
    """创建套件/用例目录，返回最终目录。"""
    case_dir = root / suite / case
    case_dir.mkdir(parents=True, exist_ok=True)
    return case_dir


def artifact_path(root: Path, suite: str, case: str, flavor: str, ext: str = "json") -> Path:
    """
    构造 artifacts 文件路径。
    - flavor: baseline/qcurl 等标识
    """
    return ensure_case_dir(root, suite, case) / f"{flavor}.{ext}"


def write_json(path: Path, payload: Dict[str, Any]) -> None:
    """以 UTF-8 写出 JSON，并在 gate 运行中绑定 run 与 pytest nodeid。"""

    run_id = os.environ.get("QCURL_LC_RUN_ID", "").strip()
    execution_token = os.environ.get("QCURL_LC_EXECUTION_TOKEN", "").strip()
    identity_json = os.environ.get("QCURL_LC_EVIDENCE_IDENTITY_JSON", "").strip()
    current_test = os.environ.get("PYTEST_CURRENT_TEST", "").strip()
    if run_id and current_test:
        nodeid = current_test.rsplit(" (", 1)[0]
        gate_evidence: Dict[str, Any] = {
            "run_id": run_id,
            "pytest_nodeid": nodeid,
        }
        if execution_token:
            gate_evidence["execution_token"] = execution_token
        if identity_json:
            try:
                gate_evidence["identity"] = json.loads(identity_json)
            except json.JSONDecodeError as exc:
                raise ValueError("QCURL_LC_EVIDENCE_IDENTITY_JSON 不是有效 JSON") from exc
        payload = {
            **payload,
            "gate_evidence": gate_evidence,
        }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> Tuple[int, str]:
    data = path.read_bytes()
    return len(data), sha256_bytes(data)


def normalize_headers(headers: Dict[str, str]) -> Dict[str, str]:
    """
    关键头规范化：key 小写，去掉首尾空白。值不做拆分，由调用方保证顺序/合并策略。
    """
    return {k.lower().strip(): v.strip() for k, v in headers.items()}


def build_request_semantic(method: str,
                           url: str,
                           headers: Optional[Dict[str, str]] = None,
                           body: Optional[bytes] = None,
                           raw_lines: Optional[Iterable[str]] = None) -> Dict[str, Any]:
    headers_norm = normalize_headers(headers or {})
    body_len = len(body) if body else 0
    body_hash = sha256_bytes(body) if body else ""
    out: Dict[str, Any] = {
        "method": method.upper(),
        "url": url,
        "headers": headers_norm,
        "body_len": body_len,
        "body_sha256": body_hash,
    }
    if raw_lines is not None:
        lines = [str(line) for line in raw_lines]
        blob = "\n".join(lines).encode("utf-8")
        out["headers_raw_lines"] = lines
        out["headers_raw_len"] = int(len(blob))
        out["headers_raw_sha256"] = sha256_bytes(blob)
        out["headers_semantic"] = headers_norm
    return out


def build_response_summary(status: int,
                           http_version: str,
                           headers: Optional[Dict[str, str]] = None,
                           body: Optional[bytes] = None,
                           body_files: Optional[Iterable[Path]] = None) -> Dict[str, Any]:
    """
    响应侧摘要：优先直接传 body；如为下载落盘场景，可传 body_files 计算 len/hash。
    """
    headers_norm = normalize_headers(headers or {})
    if body is not None:
        body_len = len(body)
        body_hash = sha256_bytes(body)
    elif body_files:
        total_len = 0
        hasher = hashlib.sha256()
        for f in body_files:
            chunk = f.read_bytes()
            total_len += len(chunk)
            hasher.update(chunk)
        body_len = total_len
        body_hash = hasher.hexdigest()
    else:
        body_len = 0
        body_hash = ""
    return {
        "status": status,
        "http_version": http_version,
        "headers": headers_norm,
        "body_len": body_len,
        "body_sha256": body_hash,
    }
