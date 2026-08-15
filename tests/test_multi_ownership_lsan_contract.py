from __future__ import annotations

import re
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
POISON_CASES = (
    "internal",
    "bad-easy",
    "bad-handle",
    "unknown-done",
    "concurrent-quarantine",
)


def test_lsan_suppression_is_limited_to_permanent_poison_cases() -> None:
    cmake_source = (REPO_ROOT / "tests" / "qcurl" / "CMakeLists.txt").read_text(
        encoding="utf-8"
    )

    poison_declaration = re.search(
        r"set\(_qcurl_multi_poison_cases\s+(?P<body>.*?)\s*\)",
        cmake_source,
        re.DOTALL,
    )
    assert poison_declaration is not None
    assert tuple(poison_declaration.group("body").split()) == POISON_CASES
    assert (
        "foreach(_ownership_case IN ITEMS recursive-once owner-thread-rejection "
        "${_qcurl_multi_poison_cases}"
        in cmake_source
    )
    assert "if(_ownership_case IN_LIST _qcurl_multi_poison_cases)" in cmake_source
    assert '"LSAN_OPTIONS=${_qcurl_multi_poison_lsan_options}"' in cmake_source
    assert "detect_leaks=" not in cmake_source


def test_lsan_suppression_names_only_the_intentional_retention_root() -> None:
    suppression_path = REPO_ROOT / "tests" / "qcurl" / "multi_ownership_lsan.supp"
    rules = [
        line.strip()
        for line in suppression_path.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]

    assert rules == ["leak:QCurl::QCCurlMultiManager::retainPoisonedObjectGraph"]
