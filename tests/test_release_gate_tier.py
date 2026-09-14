from __future__ import annotations

import json

import pytest

from scripts import release_gate_steps, run_release_gate
from scripts.release_gate_model import GateStep, GateTier


@pytest.mark.parametrize(
    ("text", "tier", "rank"),
    [("fast", GateTier.FAST, 0), ("strict", GateTier.STRICT, 1), ("full", GateTier.FULL, 2)],
)
def test_tier_cli_and_json_keep_external_text(text, tier, rank, capsys) -> None:
    args = run_release_gate.build_parser().parse_args(["--tier", text])
    assert args.tier is tier
    assert tier.rank == rank
    step = GateStep("probe", tier, ["true"], "测试步骤")

    run_release_gate._write_plan(args, [step])

    plan = json.loads(capsys.readouterr().out)
    assert plan["tier"] == text
    assert plan["steps"] == [
        {
            "name": "probe",
            "tier": text,
            "description": "测试步骤",
            "command": ["true"],
            "producerTreeId": "",
            "requiredArtifactIds": [],
        }
    ]


@pytest.mark.parametrize("tier", ["fast", "fasst", "FULL", "", None, 0])
def test_step_rejects_untyped_or_invalid_tier_at_construction(tier) -> None:
    with pytest.raises(TypeError, match="GateStep.tier must be a GateTier"):
        GateStep("invalid", tier, ["true"], "测试步骤")


@pytest.mark.parametrize("text", ["fasst", "FULL", ""])
def test_cli_rejects_invalid_tier(text) -> None:
    with pytest.raises(SystemExit) as error:
        run_release_gate.build_parser().parse_args(["--tier", text])
    assert error.value.code == 2


def test_internal_plan_boundary_rejects_invalid_tier_before_filtering() -> None:
    args = run_release_gate.build_parser().parse_args([])
    args.tier = "fasst"
    with pytest.raises(ValueError, match="fasst"):
        release_gate_steps.build_steps(args)
    with pytest.raises(ValueError, match="fasst"):
        run_release_gate._selected_steps(args)
