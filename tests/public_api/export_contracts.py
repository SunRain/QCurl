"""Export-file checks for QCurl install/package contracts."""

from __future__ import annotations

from pathlib import Path
import re


STATIC_PUBLIC_DEPENDENCIES = {
    "CURL::libcurl": "CURL",
    "ZLIB::ZLIB": "ZLIB",
}


def check_export_contract(stage_dir: Path, *, fail_func) -> int:
    """Verify installed export files expose only expected dependency targets."""

    target_files = sorted(stage_dir.rglob("QCurlTargets*.cmake"))
    if not target_files:
        return fail_func(f"no QCurlTargets*.cmake files found under {stage_dir}")

    content_by_file = {target_file: target_file.read_text(encoding="utf-8") for target_file in target_files}
    config_files = sorted(stage_dir.rglob("QCurlConfig.cmake"))
    config_content = "\n".join(path.read_text(encoding="utf-8") for path in config_files)
    static_export = any(
        re.search(r"add_library\s*\(\s*QCurl::QCurl\s+STATIC\s+IMPORTED", content)
        for content in content_by_file.values()
    )

    forbidden_patterns = [
        ("QCurl::libcurl_shared", re.compile(r"\bQCurl::libcurl_shared\b")),
        ("raw libcurl dependency",
         re.compile(r"IMPORTED_LINK_DEPENDENT_LIBRARIES[^\n]*libcurl", re.IGNORECASE)),
    ]
    if not static_export:
        forbidden_patterns.append(("CURL::libcurl", re.compile(r"\bCURL::libcurl\b")))

    violations: list[str] = []
    for target_file, content in content_by_file.items():
        for rule_name, pattern in forbidden_patterns:
            if pattern.search(content):
                violations.append(f"{target_file.name}: {rule_name}")
        if static_export:
            for target, dependency in STATIC_PUBLIC_DEPENDENCIES.items():
                if re.search(rf"\b{re.escape(target)}\b", content) and (
                    f"find_dependency({dependency}" not in config_content
                ):
                    violations.append(
                        f"{target_file.name}: static export depends on {target} but "
                        f"QCurlConfig.cmake does not find_dependency({dependency})"
                    )

    if violations:
        print("\n".join(violations), file=__import__("sys").stderr)
        return 1

    print("[public_api] export contract passed")
    return 0
