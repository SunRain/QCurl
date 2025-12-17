"""
服务端日志收集（LC-12）：
- 默认不作为 Gate 断言，仅用于失败时快速定位问题。
- 通过环境变量显式开启：QCURL_LC_COLLECT_LOGS=1
"""

from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Dict, Optional


def should_collect_service_logs() -> bool:
    return os.environ.get("QCURL_LC_COLLECT_LOGS", "").strip() == "1"


def collect_service_logs(logs: Dict[str, Path], dest: Path) -> Dict[str, str]:
    """
    将 httpd/nghttpx/ws 等日志复制到目标目录，返回目标文件路径映射。
    - 仅用于调试，不作为 artifacts 对比字段。
    """
    dest.mkdir(parents=True, exist_ok=True)
    copied: Dict[str, str] = {}
    for name, path in logs.items():
        try:
            if path.exists():
                target = dest / path.name
                target.write_bytes(path.read_bytes())
                copied[name] = str(target)
        except OSError:
            # 调试增强：收集失败不应影响原始断言结果
            continue
    return copied


def collect_service_logs_for_case(env,
                                 suite: str,
                                 case: str,
                                 logs: Dict[str, Path],
                                 *,
                                 meta: Optional[Dict[str, str]] = None) -> Dict[str, str]:
    """
    将服务端日志收集到对应 case 的 artifacts 目录下：
      curl/tests/http/gen/artifacts/<suite>/<case>/service_logs/
    """
    from .artifacts import artifacts_root, ensure_case_dir  # local import 避免循环依赖

    dest = ensure_case_dir(artifacts_root(env), suite=suite, case=case) / "service_logs"
    copied = collect_service_logs(logs, dest)
    if meta:
        (dest / "meta.json").write_text(
            json.dumps(meta, ensure_ascii=False, indent=2),
            encoding="utf-8",
        )
    return copied

