from __future__ import annotations

import json
from pathlib import Path

from scripts.run_uce_gate import main
from scripts.uce.manifest import create_manifest
from scripts.uce_gate.planner import ctbp_required_kinds
from scripts.uce_gate.planner import ctbp_required_runners
from scripts.uce_gate.planner import build_tier_plan
from scripts.uce_gate.planner import dci_seed_matrix
from scripts.uce_gate.planner import timeline_required_providers
from scripts.uce_gate.planner import validate_required_artifacts
from scripts.uce_gate.qt_contracts import run_bp_contract
from scripts.uce_gate.evidence import prepare_evidence_layout
from scripts.uce_gate.evidence import resolve_evidence_layout
from scripts.uce_gate.evidence import write_policy_report
from scripts.uce.manifest import add_artifact
from tests.uce.hes.validate import validate_hes


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
        "public_api_slow",
        "capability",
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


def test_evidence_layout_resolves_relative_root_and_prepares_dirs(tmp_path: Path) -> None:
    repo_root = tmp_path / "repo"
    build_dir = repo_root / "build"
    repo_root.mkdir()
    build_dir.mkdir()

    layout = resolve_evidence_layout(
        repo_root,
        build_dir,
        run_id="run-layout",
        evidence_root_arg="relative-evidence",
    )
    prepare_evidence_layout(layout)

    assert layout.evidence_root == repo_root / "relative-evidence"
    assert layout.evidence_dir == repo_root / "relative-evidence" / "run-layout"
    assert layout.manifest_path == layout.evidence_dir / "manifest.json"
    assert layout.policy_report_path == layout.evidence_dir / "policy_violations.json"
    assert layout.tar_path == layout.evidence_root / "run-layout.tar.gz"
    assert layout.logs_dir.is_dir()
    assert layout.meta_dir.is_dir()
    assert layout.reports_dir.is_dir()
    assert layout.netproof_dir.is_dir()
    assert layout.lc_dir.is_dir()


def test_write_policy_report_preserves_schema(tmp_path: Path) -> None:
    report_path = tmp_path / "policy_violations.json"

    write_policy_report(
        report_path,
        tier="nightly",
        policy_violations=["gate_offline_failed"],
        missing_required_artifacts=["manifest"],
    )

    payload = json.loads(report_path.read_text(encoding="utf-8"))
    assert payload["tier"] == "nightly"
    assert payload["policy_violations"] == ["gate_offline_failed"]
    assert payload["missing_required_artifacts"] == ["manifest"]
    assert payload["generated_at_utc"].endswith("Z")


def test_timeline_required_providers_follow_tier() -> None:
    assert timeline_required_providers("pr") == {"qt"}
    assert timeline_required_providers("nightly") == {"qt", "libcurl_consistency"}
    assert timeline_required_providers("soak") == {"qt", "libcurl_consistency"}


def test_ctbp_requirements_are_stable() -> None:
    assert ctbp_required_runners() == {"libcurl", "qcurl"}
    assert ctbp_required_kinds() == {"connection_reuse", "tls_boundary"}
    contract = json.loads(
        (Path(__file__).parent / "uce" / "contracts" / "ctbp@v1.yaml").read_text(
            encoding="utf-8"
        )
    )
    assert set(contract["required_runners"]) == ctbp_required_runners()
    assert set(contract["required_kinds"]) == ctbp_required_kinds()


def test_hes_nightly_contract_accepts_current_sized_upload_evidence(tmp_path: Path) -> None:
    artifacts_root = tmp_path / "artifacts"
    valid_hes = {
        "accept_encoding": {
            "kind": "accept_encoding",
            "request_accept_encoding": "gzip",
            "response_content_encoding": "gzip",
            "body_len": 16,
        },
        "raw_headers": {
            "kind": "raw_headers",
            "headers_raw_lines": ["Set-Cookie: a=1", "Set-Cookie: b=2"],
            "set_cookie_count": 2,
            "x_dupe_count": 2,
        },
        "expect_100_continue": {
            "kind": "expect_100_continue",
            "statuses": [417, 200],
            "first_expect_header": "100-continue",
            "second_expect_present": False,
        },
        "blocking_extras_sized_upload": {
            "kind": "blocking_extras_sized_upload",
            "transfer_encoding": "",
            "content_length": "4096",
            "body_len": 16,
        },
    }
    for runner, artifact_name in (("libcurl", "baseline.json"), ("qcurl", "qcurl.json")):
        for kind, hes in valid_hes.items():
            case_dir = artifacts_root / f"{runner}-{kind}"
            case_dir.mkdir(parents=True)
            (case_dir / artifact_name).write_text(
                json.dumps({"runner": runner, "hes": hes}),
                encoding="utf-8",
            )

    report = validate_hes(
        Path(__file__).parent / "uce" / "contracts" / "hes@v1.yaml",
        [artifacts_root],
        "nightly",
    )

    assert report["required_runners"] == ["libcurl", "qcurl"]
    assert report["required_kinds"] == [
        "accept_encoding",
        "blocking_extras_sized_upload",
        "expect_100_continue",
        "raw_headers",
    ]
    assert report["policy_violations"] == []


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
                "(out_dir / 'argv.json').write_text(json.dumps(sys.argv[1:]), encoding='utf-8')",
                "(out_dir / 'httpbin_url.txt').write_text(os.environ.get('QCURL_HTTPBIN_URL',''), encoding='utf-8')",
                "rows = [",
                "  {'schema':'qcurl-uce/dci-evidence@v1','case':'testAsyncDownloadBackpressure','stream':'testAsyncDownloadBackpressure:bp-user-pause','event':'request_headers','seq':1},",
                "  {'schema':'qcurl-uce/dci-evidence@v1','case':'testAsyncDownloadBackpressure','stream':'testAsyncDownloadBackpressure:bp-user-pause','event':'response_headers','seq':2},",
                    "  {'schema':'qcurl-uce/dci-evidence@v1','case':'testAsyncDownloadBackpressure','stream':'testAsyncDownloadBackpressure:bp-user-pause','event':'backpressure_on','seq':3,'buffered_bytes':20000,'limit_bytes':16384,'bytes_delivered_total':20000,'bytes_written_total':0},",
                    "  {'schema':'qcurl-uce/dci-evidence@v1','case':'testAsyncDownloadBackpressure','stream':'testAsyncDownloadBackpressure:bp-user-pause','event':'pause_req','seq':4,'bytes_delivered_total':20000,'bytes_written_total':0},",
                    "  {'schema':'qcurl-uce/dci-evidence@v1','case':'testAsyncDownloadBackpressure','stream':'testAsyncDownloadBackpressure:bp-user-pause','event':'first_byte','seq':5,'bytes_delivered_total':20000,'bytes_written_total':4096,'chunk_len':0},",
                    "  {'schema':'qcurl-uce/dci-evidence@v1','case':'testAsyncDownloadBackpressure','stream':'testAsyncDownloadBackpressure:bp-user-pause','event':'body_chunk','seq':6,'bytes_delivered_total':20000,'bytes_written_total':4096,'chunk_len':4096},",
                    "  {'schema':'qcurl-uce/dci-evidence@v1','case':'testAsyncDownloadBackpressure','stream':'testAsyncDownloadBackpressure:bp-user-pause','event':'backpressure_off','seq':7,'buffered_bytes':7000,'limit_bytes':16384,'bytes_delivered_total':20000,'bytes_written_total':4096},",
                    "  {'schema':'qcurl-uce/dci-evidence@v1','case':'testAsyncDownloadBackpressure','stream':'testAsyncDownloadBackpressure:bp-user-pause','event':'pause_effective','seq':8,'bytes_delivered_total':20000,'bytes_written_total':4096},",
                    "  {'schema':'qcurl-uce/dci-evidence@v1','case':'testAsyncDownloadBackpressure','stream':'testAsyncDownloadBackpressure:bp-user-pause','event':'resume_req','seq':9,'bytes_delivered_total':20000,'bytes_written_total':4096},",
                    "  {'schema':'qcurl-uce/dci-evidence@v1','case':'testAsyncDownloadBackpressure','stream':'testAsyncDownloadBackpressure:bp-user-pause','event':'body_complete','seq':10,'bytes_delivered_total':262144,'bytes_written_total':262144,'chunk_len':262144},",
                    "  {'schema':'qcurl-uce/dci-evidence@v1','case':'testAsyncDownloadBackpressure','stream':'testAsyncDownloadBackpressure:bp-user-pause','event':'finished','seq':11,'result':'pass','status':200,'body_len':262144},",
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

    results, violations = run_bp_contract(
        repo_root,
        build_dir,
        evidence_dir,
        manifest,
        tier="nightly",
        run_id="run-bp",
        runtime_env={"QCURL_HTTPBIN_URL": "http://127.0.0.1:18080"},
    )

    assert [item.gate_id for item in results] == ["bp_testAsyncDownloadBackpressure"]
    assert violations == []
    assert manifest["contracts"]["bp@v1"]["result"] == "pass"
    assert manifest["artifacts"]["bp_contract"]["required"] is True
    assert manifest["artifacts"]["bp_report"]["required"] is True
    assert manifest["artifacts"]["bp_evidence_dir"]["required"] is True
    argv_path = (
        build_dir
        / "test-artifacts"
        / "bp"
        / "run-bp"
        / "testAsyncDownloadBackpressure"
        / "argv.json"
    )
    assert json.loads(argv_path.read_text(encoding="utf-8")) == [
        "-o",
        "-,txt",
        "testAsyncDownloadBackpressure",
    ]
    assert (argv_path.parent / "httpbin_url.txt").read_text(encoding="utf-8") == (
        "http://127.0.0.1:18080"
    )
