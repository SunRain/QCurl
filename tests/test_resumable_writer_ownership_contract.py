from __future__ import annotations

import re
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
WRITER_HEADER = REPO_ROOT / "src/private/QCNetworkResumableDownloadWriter_p.h"
JOB_SOURCE = REPO_ROOT / "src/QCNetworkResumableDownloadJob.cpp"


def test_writer_is_a_non_shared_non_qobject_unique_owner() -> None:
    header = WRITER_HEADER.read_text(encoding="utf-8")

    assert "class ResumableDownloadWriter final" in header
    assert "Q_OBJECT" not in header
    assert "QSharedPointer" not in header
    assert "ResumableDownloadWriteContext" not in header
    assert re.search(r"\bQFile\s+m_file\s*;", header)
    assert re.search(r"\bQSaveFile\s+m_overwriteFile\s*;", header)
    assert "Q_DISABLE_COPY_MOVE(ResumableDownloadWriter)" in header


def test_job_is_the_only_writer_owner_and_callbacks_do_not_copy_it() -> None:
    source = JOB_SOURCE.read_text(encoding="utf-8")

    assert "std::unique_ptr<Internal::ResumableDownloadWriter> writer;" in source
    assert "std::make_unique<Internal::ResumableDownloadWriter>" in source
    assert "makeResumableDownloadWriteContext" not in source
    assert not re.search(r"\[[^\]]*\b(?:context|writer)\b[^\]]*\]", source)


def test_job_destruction_disconnects_callbacks_before_releasing_writer() -> None:
    source = JOB_SOURCE.read_text(encoding="utf-8")
    destructor = re.search(
        r"QCNetworkResumableDownloadJob::~QCNetworkResumableDownloadJob\(\)\s*\{(?P<body>.*?)\n\}",
        source,
        re.DOTALL,
    )

    assert destructor is not None
    body = destructor.group("body")
    disconnect_position = body.find("QObject::disconnect")
    reset_position = body.find("writer.reset()")
    assert disconnect_position >= 0
    assert reset_position > disconnect_position
