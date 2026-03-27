from __future__ import annotations

from pathlib import Path

from scripts.run_uce_gate import build_tier_plan
from scripts.run_uce_gate import ctbp_required_kinds
from scripts.run_uce_gate import ctbp_required_runners
from scripts.run_uce_gate import create_manifest
from scripts.run_uce_gate import dci_seed_matrix
from scripts.run_uce_gate import main
from scripts.run_uce_gate import _run_bp_contract
from scripts.run_uce_gate import timeline_required_providers
from scripts.run_uce_gate import validate_required_artifacts
from scripts.uce.manifest import add_artifact


def test_build_tier_plan_for_pr() -> None:
    plan = build_tier_plan("pr")

    assert [item.gate_id for item in plan] == [
        "ctest_strict_offline",
        "libcurl_consistency_p0",
        "libcurl_consistency_p1",
    ]


def test_build_tier_plan_for_nightly_includes_env_and_p1() -> None:
    plan = build_tier_plan("nightly")

    assert [item.gate_id for item in plan] == [
        "ctest_strict_offline",
        "libcurl_consistency_p0",
        "libcurl_consistency_p1",
        "ctest_strict_env",
        "libcurl_consistency_p2",
    ]


def test_validate_required_artifacts_finds_missing_relative_paths(tmp_path: Path) -> None:
    manifest = create_manifest(
        gate_id="uce",
        tier="pr",
        run_id="run-3",
        repo_root="/repo",
        build_dir="/repo/build",
        evidence_dir=str(tmp_path),
        tar_gz="/repo/build/evidence/uce/run-3.tar.gz",
    )
    add_artifact(manifest, artifact_id="present", path="present.txt", kind="report", required=True)
    add_artifact(manifest, artifact_id="missing", path="missing.txt", kind="report", required=True)
    (tmp_path / "present.txt").write_text("ok\n", encoding="utf-8")

    missing = validate_required_artifacts(manifest, tmp_path)

    assert missing == ["missing"]


def test_timeline_required_providers_follow_tier() -> None:
    assert timeline_required_providers("pr") == {"qt"}
    assert timeline_required_providers("nightly") == {"qt", "libcurl_consistency"}
    assert timeline_required_providers("soak") == {"qt", "libcurl_consistency"}


def test_ctbp_requirements_are_stable() -> None:
    assert ctbp_required_runners() == {"baseline", "qcurl"}
    assert ctbp_required_kinds() == {"connection_reuse", "tls_boundary"}


def test_dci_seed_matrix_is_fixed_per_tier() -> None:
    nightly = dci_seed_matrix("nightly")
    soak = dci_seed_matrix("soak")

    assert nightly == {
        "testAsyncMockChaosPauseResume": [17, 29],
        "testAsyncMockChaosCancel": [5, 19],
        "testAsyncMockChaosDeleteLater": [23, 41],
    }
    assert soak == {
        "testAsyncMockChaosPauseResume": [17, 29, 43],
        "testAsyncMockChaosCancel": [5, 19, 31],
        "testAsyncMockChaosDeleteLater": [23, 41, 47],
    }


def test_main_returns_error_for_missing_build_dir(tmp_path: Path) -> None:
    rc = main(
        [
            "--tier",
            "pr",
            "--build-dir",
            str(tmp_path / "missing-build"),
            "--run-id",
            "smoke",
            "--evidence-root",
            str(tmp_path / "evidence"),
        ]
    )

    assert rc == 2


def test_bp_contract_registers_manifest_entries(tmp_path: Path) -> None:
    repo_root = Path(__file__).resolve().parent.parent
    build_dir = tmp_path / "build"
    evidence_dir = tmp_path / "evidence" / "uce" / "run-bp"
    qt_bin = build_dir / "tests" / "tst_QCNetworkReply"
    qt_bin.parent.mkdir(parents=True, exist_ok=True)
    qt_bin.write_text(
        "\n".join(
            [
                "#!/usr/bin/env python3",
                "import json, os, sys",
                "from pathlib import Path",
                "out_dir = Path(os.environ.get('QCURL_LC_OUT_DIR',''))",
                "out_dir.mkdir(parents=True, exist_ok=True)",
                "rows = [",
                "  {'schema':'qcurl-uce/dci-evidence@v1','case':'testAsyncDownloadBackpressure','stream':'testAsyncDownloadBackpressure:bp-user-pause','event':'request_headers','seq':1},",
                "  {'schema':'qcurl-uce/dci-evidence@v1','case':'testAsyncDownloadBackpressure','stream':'testAsyncDownloadBackpressure:bp-user-pause','event':'response_headers','seq':2},",
                "  {'schema':'qcurl-uce/dci-evidence@v1','case':'testAsyncDownloadBackpressure','stream':'testAsyncDownloadBackpressure:bp-user-pause','event':'backpressure_on','seq':3,'buffered_bytes':20000,'limit_bytes':16384,'bytes_delivered_total':20000,'bytes_written_total':0},",
                "  {'schema':'qcurl-uce/dci-evidence@v1','case':'testAsyncDownloadBackpressure','stream':'testAsyncDownloadBackpressure:bp-user-pause','event':'pause_effective','seq':4,'bytes_delivered_total':20000,'bytes_written_total':0},",
                "  {'schema':'qcurl-uce/dci-evidence@v1','case':'testAsyncDownloadBackpressure','stream':'testAsyncDownloadBackpressure:bp-user-pause','event':'first_byte','seq':5,'bytes_delivered_total':20000,'bytes_written_total':4096,'chunk_len':0},",
                "  {'schema':'qcurl-uce/dci-evidence@v1','case':'testAsyncDownloadBackpressure','stream':'testAsyncDownloadBackpressure:bp-user-pause','event':'body_chunk','seq':6,'bytes_delivered_total':20000,'bytes_written_total':4096,'chunk_len':4096},",
                "  {'schema':'qcurl-uce/dci-evidence@v1','case':'testAsyncDownloadBackpressure','stream':'testAsyncDownloadBackpressure:bp-user-pause','event':'backpressure_off','seq':7,'buffered_bytes':7000,'limit_bytes':16384,'bytes_delivered_total':20000,'bytes_written_total':4096},",
                "  {'schema':'qcurl-uce/dci-evidence@v1','case':'testAsyncDownloadBackpressure','stream':'testAsyncDownloadBackpressure:bp-user-pause','event':'resume_req','seq':8,'bytes_delivered_total':24000,'bytes_written_total':4096},",
                "  {'schema':'qcurl-uce/dci-evidence@v1','case':'testAsyncDownloadBackpressure','stream':'testAsyncDownloadBackpressure:bp-user-pause','event':'body_complete','seq':9,'bytes_delivered_total':262144,'bytes_written_total':262144,'chunk_len':262144},",
                "  {'schema':'qcurl-uce/dci-evidence@v1','case':'testAsyncDownloadBackpressure','stream':'testAsyncDownloadBackpressure:bp-user-pause','event':'finished','seq':10,'result':'pass','status':200,'body_len':262144},",
                "]",
                "path = out_dir / 'dci_evidence_testAsyncDownloadBackpressure_testAsyncDownloadBackpressure_bp-user-pause_0.jsonl'",
                "with path.open('w', encoding='utf-8') as fh:",
                "  for row in rows:",
                "    fh.write(json.dumps(row, ensure_ascii=False) + '\\n')",
                "sys.exit(0)",
                "",
            ]
        )
        + "\n",
        encoding="utf-8",
    )
    qt_bin.chmod(0o755)

    manifest = create_manifest(
        gate_id="uce",
        tier="nightly",
        run_id="run-bp",
        repo_root=str(repo_root),
        build_dir=str(build_dir),
        evidence_dir=str(evidence_dir),
        tar_gz=str(tmp_path / "bundle.tar.gz"),
    )

    results, violations = _run_bp_contract(
        repo_root,
        build_dir,
        evidence_dir,
        manifest,
        tier="nightly",
        run_id="run-bp",
    )

    assert [item.gate_id for item in results] == ["bp_testAsyncDownloadBackpressure"]
    assert violations == []
    assert manifest["contracts"]["bp@v1"]["result"] == "pass"
    assert manifest["artifacts"]["bp_contract"]["required"] is True
    assert manifest["artifacts"]["bp_report"]["required"] is True
    assert manifest["artifacts"]["bp_evidence_dir"]["required"] is True
