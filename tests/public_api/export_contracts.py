"""Export-file checks for QCurl install/package contracts."""

from __future__ import annotations

from pathlib import Path
import re


STATIC_PUBLIC_DEPENDENCIES = {
    "CURL::libcurl": "CURL",
}

STATIC_OTHER_EXTRAS_DEPENDENCIES = {
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
    zlib_find_dependency = re.search(r"find_dependency\s*\(\s*ZLIB\b", config_content)
    unconditional_zlib_dependency = (
        zlib_find_dependency is not None
        and "OtherExtras" not in config_content[:zlib_find_dependency.start()]
    )
    qtnetwork_find_dependency = re.search(
        r"find_dependency\s*\(\s*Qt6\s+REQUIRED\s+COMPONENTS\s+Network\b",
        config_content,
    )
    unconditional_qtnetwork_dependency = (
        qtnetwork_find_dependency is not None
        and "OtherExtras" not in config_content[:qtnetwork_find_dependency.start()]
    )
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
        core_properties = re.search(
            r"set_target_properties\s*\(\s*QCurl::QCurl\s+PROPERTIES(?P<body>.*?)\)",
            content,
            re.DOTALL,
        )
        core_zlib_dep = (
            core_properties is not None
            and re.search(
                r"INTERFACE_LINK_LIBRARIES[^\n]*\bZLIB::ZLIB\b",
                core_properties.group("body"),
            )
        )
        if core_zlib_dep:
            violations.append(f"{target_file.name}: Core export must not expose ZLIB::ZLIB")
        core_qtnetwork_dep = (
            core_properties is not None
            and re.search(
                r"INTERFACE_LINK_LIBRARIES[^\n]*\bQt6::Network\b",
                core_properties.group("body"),
            )
        )
        if core_qtnetwork_dep:
            violations.append(f"{target_file.name}: Core export must not expose Qt6::Network")
        if unconditional_qtnetwork_dependency:
            violations.append(
                "QCurlConfig.cmake: Core consumer must not unconditionally find Qt6::Network"
            )
        if static_export and unconditional_zlib_dependency:
            violations.append("QCurlConfig.cmake: Core static consumer must not unconditionally find ZLIB")
        if static_export:
            for target, dependency in STATIC_PUBLIC_DEPENDENCIES.items():
                if re.search(rf"\b{re.escape(target)}\b", content) and (
                    f"find_dependency({dependency}" not in config_content
                ):
                    violations.append(
                        f"{target_file.name}: static export depends on {target} but "
                        f"QCurlConfig.cmake does not find_dependency({dependency})"
                    )
            for target, dependency in STATIC_OTHER_EXTRAS_DEPENDENCIES.items():
                other_extras_dep = re.search(
                    rf"INTERFACE_LINK_LIBRARIES[^\n]*QCurl::QCurl[^\n]*{re.escape(target)}",
                    content,
                )
                if other_extras_dep and f"find_dependency({dependency}" not in config_content:
                    violations.append(
                        f"{target_file.name}: static OtherExtras depends on {target} but "
                        f"QCurlConfig.cmake does not find_dependency({dependency})"
                    )

    required_targets = {
        "QCurl::QCurl",
        "QCurl::BlockingExtras",
        "QCurl::TestSupport",
        "QCurl::OtherExtras",
    }
    combined_targets = "\n".join(content_by_file.values())
    for target in sorted(required_targets):
        if not re.search(rf"\badd_library\s*\(\s*{re.escape(target)}\b", combined_targets):
            violations.append(f"missing exported target: {target}")

    if violations:
        print("\n".join(violations), file=__import__("sys").stderr)
        return 1

    print("[public_api] export contract passed")
    return 0
