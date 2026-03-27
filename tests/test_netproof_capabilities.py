from __future__ import annotations

from scripts.netproof_capabilities import build_report
from scripts.netproof_capabilities import build_tier_summary


def test_build_tier_summary_marks_missing_required_provider() -> None:
    providers = {
        "strace": {"available": False, "downgrade_reason": "missing strace"},
        "tc": {"available": True, "downgrade_reason": None},
        "netns": {"available": False, "downgrade_reason": "missing privileges"},
    }

    summary = build_tier_summary("nightly", providers)

    assert summary["capability"] == "missing_required"
    assert summary["required_providers"] == ["strace"]
    assert summary["missing_required_providers"] == ["strace"]
    assert summary["optional_missing_providers"] == ["netns"]
    assert "missing strace" in summary["downgrade_reasons"]


def test_build_tier_summary_degrades_when_only_optional_providers_missing() -> None:
    providers = {
        "strace": {"available": True, "downgrade_reason": None},
        "tc": {"available": False, "downgrade_reason": "missing tc"},
        "netns": {"available": True, "downgrade_reason": None},
    }

    summary = build_tier_summary("pr", providers)

    assert summary["capability"] == "degraded"
    assert summary["required_providers"] == []
    assert summary["missing_required_providers"] == []
    assert summary["optional_missing_providers"] == ["tc"]


def test_build_report_can_filter_single_tier() -> None:
    report = build_report(selected_tier="pr")

    assert report["schema_version"] == 1
    assert set(report["providers"]) == {"strace", "tc", "netns"}
    assert set(report["tiers"]) == {"pr"}
