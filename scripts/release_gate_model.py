"""Shared release-gate value types."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class GateStep:
    """Describe one ordered no-git release gate command."""

    name: str
    tier: str
    command: list[str]
    description: str
    producer_tree_id: str = ""
    required_artifact_ids: tuple[str, ...] = ()
