"""Collect baseline-side timeline evidence from libcurl_consistency artifacts."""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Any

from tests.uce.timeline.common import add_payload_events
from tests.uce.timeline.common import load_json
from tests.uce.timeline.common import stream_identity
from tests.uce.timeline.common import write_json
from tests.uce.timeline.common import write_jsonl


def collect_from_lc(artifacts_root: Path) -> dict[str, Any]:
    """Collect normalized timeline events from `baseline.json` artifacts."""

    result: dict[str, Any] = {
        "provider": "libcurl_consistency",
        "artifacts_root": str(artifacts_root),
        "stream_count": 0,
        "event_count": 0,
        "source_files": [],
        "missing_roots": [],
        "errors": [],
        "events": [],
    }
    if not artifacts_root.exists():
        result["missing_roots"].append(str(artifacts_root))
        return result

    events: list[dict[str, Any]] = []
    for artifact_path in sorted(artifacts_root.rglob("baseline.json")):
        try:
            payload = load_json(artifact_path)
        except Exception as exc:
            result["errors"].append(f"{artifact_path}: {exc}")
            continue
        case_id, stream_id = stream_identity(artifacts_root, artifact_path, "baseline")
        stream_events: list[dict[str, Any]] = []
        add_payload_events(
            stream_events,
            provider="libcurl_consistency",
            stream_id=stream_id,
            case_id=case_id,
            source_path=artifact_path,
            payload=payload,
        )
        if not stream_events:
            continue
        events.extend(stream_events)
        result["source_files"].append(str(artifact_path))
        result["stream_count"] += 1

    result["events"] = events
    result["event_count"] = len(events)
    return result


def main(argv: list[str] | None = None) -> int:
    """CLI entry point."""

    parser = argparse.ArgumentParser(description="Collect LC timeline evidence into JSONL.")
    parser.add_argument("--artifacts-root", required=True, help="Root of curl/tests/http/gen/artifacts.")
    parser.add_argument("--output", required=True, help="Output JSONL path.")
    parser.add_argument("--report", default="", help="Optional JSON report path.")
    args = parser.parse_args(argv)

    result = collect_from_lc(Path(args.artifacts_root))
    write_jsonl(Path(args.output), result["events"])
    if args.report:
        report_payload = {key: value for key, value in result.items() if key != "events"}
        write_json(Path(args.report), report_payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
