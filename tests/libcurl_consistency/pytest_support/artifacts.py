"""一致性 artifacts 路径、字段构造与写入工具。"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any, Dict, Iterable, Optional, Tuple


def artifacts_root(env) -> Path:
    """默认将 artifacts 放在 testenv 的 gen_dir 下，避免污染源码树。"""
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
    """以 utf-8 写出 JSON。"""
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
                           body: Optional[bytes] = None) -> Dict[str, Any]:
    headers_norm = normalize_headers(headers or {})
    body_len = len(body) if body else 0
    body_hash = sha256_bytes(body) if body else ""
    return {
        "method": method.upper(),
        "url": url,
        "headers": headers_norm,
        "body_len": body_len,
        "body_sha256": body_hash,
    }


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
