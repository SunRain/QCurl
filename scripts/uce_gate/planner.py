"""Planning helpers for scripts/run_uce_gate.py."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class GateSpec:
    """Describe a single UCE gate step."""

    gate_id: str
    kind: str
    selector: str
    policy_code: str
    requires_httpbin: bool = False


def dci_seed_matrix(tier: str) -> dict[str, list[int]]:
    """Return the fixed DCI seed matrix for a tier."""

    nightly = {
        "testAsyncMockChaosPauseResume": [17, 29],
        "testAsyncMockChaosCancel": [5, 19],
        "testAsyncMockChaosDeleteLater": [23, 41],
    }
    if tier != "soak":
        return nightly

    return {
        "testAsyncMockChaosPauseResume": [17, 29, 43],
        "testAsyncMockChaosCancel": [5, 19, 31],
        "testAsyncMockChaosDeleteLater": [23, 41, 47],
    }


def timeline_required_providers(tier: str) -> set[str]:
    """Return providers required by the TLC contract for a tier."""

    return {"qt"} if tier == "pr" else {"qt", "libcurl_consistency"}


def ctbp_required_runners() -> set[str]:
    """Return runners that must provide CTBP evidence."""

    return {"baseline", "qcurl"}


def ctbp_required_kinds() -> set[str]:
    """Return CTBP evidence kinds that must be present."""

    return {"connection_reuse", "tls_boundary"}


def build_tier_plan(tier: str) -> list[GateSpec]:
    """Return the gate plan for a UCE tier."""

    plan = [
        GateSpec("ctest_strict_offline", "ctest", "offline", "gate_offline_failed"),
        GateSpec("libcurl_consistency_p0", "libcurl_consistency", "p0", "gate_libcurl_consistency_p0_failed"),
        GateSpec("libcurl_consistency_p1", "libcurl_consistency", "p1", "gate_libcurl_consistency_p1_failed"),
    ]
    if tier in {"nightly", "soak"}:
        plan.extend(
            [
                GateSpec("ctest_strict_env", "ctest", "env", "gate_env_failed", requires_httpbin=True),
                GateSpec("libcurl_consistency_p2", "libcurl_consistency", "p2", "gate_libcurl_consistency_p2_failed"),
            ]
        )
    return plan


def validate_required_artifacts(manifest: dict[str, Any], evidence_dir: Path) -> list[str]:
    """Return missing required artifact IDs."""

    missing: list[str] = []
    artifacts = manifest.get("artifacts") or {}
    if not isinstance(artifacts, dict):
        return ["artifacts"]
    for artifact_id, payload in artifacts.items():
        if not isinstance(payload, dict) or not payload.get("required"):
            continue
        raw_path = payload.get("path")
        if not isinstance(raw_path, str) or not raw_path:
            missing.append(str(artifact_id))
            continue
        artifact_path = Path(raw_path)
        if not artifact_path.is_absolute():
            artifact_path = evidence_dir / artifact_path
        if not artifact_path.exists():
            missing.append(str(artifact_id))
    return missing
