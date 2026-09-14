"""发布检查共用的级别与步骤值类型。"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum


class GateTier(Enum):
    """封闭的发布检查级别；文本值仍用于 CLI 和 JSON。"""

    FAST = "fast"
    STRICT = "strict"
    FULL = "full"

    def __str__(self) -> str:
        return self.value

    @property
    def rank(self) -> int:
        """返回从快速到完整检查的包含顺序。"""
        return tuple(GateTier).index(self)


@dataclass(frozen=True)
class GateStep:
    """Describe one ordered no-git release gate command."""

    name: str
    tier: GateTier
    command: list[str]
    description: str
    producer_tree_id: str = ""
    required_artifact_ids: tuple[str, ...] = ()

    def __post_init__(self) -> None:
        if not isinstance(self.tier, GateTier):
            raise TypeError("GateStep.tier must be a GateTier")
