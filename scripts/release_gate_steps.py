"""构造 producer-bound release gate 步骤。"""

from __future__ import annotations

import argparse
from pathlib import Path

if __package__:
    from .release_examples_steps import examples_benchmarks_steps
    from .release_gate_model import GateStep
    from .release_package_steps import package_evidence_step
    from .release_package_steps import sanitizer_steps
    from .release_package_steps import shared_package_steps
    from .release_tree_model import tree_path
else:
    from release_examples_steps import examples_benchmarks_steps
    from release_gate_model import GateStep
    from release_package_steps import package_evidence_step
    from release_package_steps import sanitizer_steps
    from release_package_steps import shared_package_steps
    from release_tree_model import tree_path


def _step(
    name: str,
    tier: str,
    command: list[str],
    description: str,
    tree_id: str,
    artifact_ids: tuple[str, ...],
) -> GateStep:
    return GateStep(name, tier, command, description, tree_id, artifact_ids)


def _shared_steps(args: argparse.Namespace) -> list[GateStep]:
    release_shared = tree_path(args, "release-shared")
    steps = shared_package_steps(args)
    test_gcc_value = getattr(args, "test_shared_gcc_build_dir", None)
    if test_gcc_value is not None:
        test_gcc = Path(test_gcc_value)
        steps.append(
            _step(
                "shared_public_api",
                "strict",
                [args.ctest, "--test-dir", str(test_gcc), "-L", "^public-api$", "--output-on-failure"],
                "run shared public API checks in the GCC test tree",
                "test-shared-gcc",
                (),
            )
        )
    steps.append(
        package_evidence_step(
            args,
            name="shared_package_evidence",
            tree_id="release-shared",
            linkage="shared",
        )
    )
    return steps


def _static_build_steps(args: argparse.Namespace) -> list[GateStep]:
    build_dir = tree_path(args, "release-static")
    return [
        _step(
            "static_configure",
            "full",
            [
                args.cmake,
                "-S",
                ".",
                "-B",
                str(build_dir),
                "-DCMAKE_BUILD_TYPE=Release",
                "-DBUILD_EXAMPLES=OFF",
                "-DBUILD_BENCHMARKS=OFF",
                "-DBUILD_TESTING=OFF",
                "-DQCURL_BUILD_SHARED_LIBS=OFF",
                "-DQCURL_BUILD_LIBCURL_CONSISTENCY=OFF",
                "-DQCURL_FORCE_DISABLE_WEBSOCKET_SUPPORT=OFF",
            ],
            "configure the independent static release tree",
            "release-static",
            (),
        ),
        _step(
            "static_package_candidate",
            "full",
            [args.python, "tests/public_api/package_gate_contracts.py", "validate-candidate", str(build_dir)],
            "reject force-disabled or capability-cropped static candidates",
            "release-static",
            (),
        ),
        _step(
            "static_build",
            "full",
            [
                args.cmake,
                "--build",
                str(build_dir),
                "--target",
                "QCurl",
                "QCurlOtherExtras",
                "QCurlTestSupport",
                "-j",
                str(args.jobs),
            ],
            "build all static release targets",
            "release-static",
            (),
        ),
    ]


def _static_package_steps(args: argparse.Namespace) -> list[GateStep]:
    """构造静态链接相关步骤。

    静态链接的 public API 验证已被 shared_public_api 和 full_ctest 覆盖；
    此处只保留 package evidence（install/consumer/lifecycle），它们确实读取静态构建目录。
    """
    steps: list[GateStep] = []
    release_static_value = getattr(args, "release_static_build_dir", None)
    if release_static_value is None:
        return steps
    release_static = Path(release_static_value)
    steps.append(
        package_evidence_step(
            args,
            name="static_package_evidence",
            tree_id="release-static",
            linkage="static",
        )
    )
    return steps


def _strict_steps(args: argparse.Namespace) -> list[GateStep]:
    test_gcc = tree_path(args, "test-shared-gcc")
    return [
        _step(
            "strict_qttest",
            "strict",
            [args.python, "scripts/ctest_strict.py", "--build-dir", str(test_gcc)],
            "run the GCC QtTest suite with skip failures",
            "test-shared-gcc",
            (),
        ),
        *examples_benchmarks_steps(args, test_gcc),
        _step(
            "deprecated_curl_api_guard",
            "strict",
            [args.python, "scripts/check_deprecated_curl_apis.py", "--curl-header", "curl/include/curl/curl.h", "--scan-root", "src"],
            "scan QCurl sources for deprecated libcurl APIs",
            "test-shared-gcc",
            (),
        ),
        _step(
            "label_matrix_guard",
            "strict",
            [args.python, "scripts/check_qcurl_label_matrix.py"],
            "validate the CTest label matrix",
            "test-shared-gcc",
            (),
        ),
        _step(
            "skip_contract_guard",
            "strict",
            [args.python, "scripts/check_skip_contract.py"],
            "validate the CTest skip contract policy",
            "test-shared-gcc",
            (),
        ),
    ]


def _full_test_steps(args: argparse.Namespace) -> list[GateStep]:
    test_gcc = tree_path(args, "test-shared-gcc")
    test_clang = tree_path(args, "test-shared-clang")
    return [
        _step(
            "full_ctest",
            "full",
            [
                args.ctest,
                "--test-dir",
                str(test_gcc),
                "--output-on-failure",
                "--output-junit",
                str(test_gcc / "evidence" / "full-ctest.xml"),
            ],
            "run the full GCC CTest suite",
            "test-shared-gcc",
            ("full_ctest_report",),
        ),
        _step(
            "clang_ctest",
            "full",
            [
                args.ctest,
                "--test-dir",
                str(test_clang),
                "--output-on-failure",
                "--output-junit",
                str(test_clang / "evidence" / "clang-ctest.xml"),
            ],
            "run the full Clang CTest suite",
            "test-shared-clang",
            ("clang_ctest_report",),
        ),
        _step(
            "libcurl_consistency_full",
            "full",
            [
                args.python,
                "tests/libcurl_consistency/run_gate.py",
                "--suite",
                "all",
                "--with-ext",
                "--build",
                "--qcurl-build",
                str(test_gcc),
                "--summary-report",
                str(
                    test_gcc
                    / "libcurl_consistency"
                    / "reports"
                    / "summary.json"
                ),
            ],
            "run full QCurl/libcurl observable consistency from the GCC test tree",
            "test-shared-gcc",
            ("parity_report",),
        ),
    ]


def _full_evidence_steps(args: argparse.Namespace) -> list[GateStep]:
    test_gcc = tree_path(args, "test-shared-gcc")
    release_shared = tree_path(args, "release-shared")
    doxygen_output = release_shared / "evidence" / "doxygen"
    return [
        _step(
            "capability_matrix_build",
            "full",
            [args.cmake, "--build", str(test_gcc), "--target", "qcurl_lc_capability_probe", "-j", str(args.jobs)],
            "build the libcurl capability probe in the GCC test tree",
            "test-shared-gcc",
            (),
        ),
        _step(
            "capability_matrix_probe",
            "full",
            [str(test_gcc / "tests" / "qcurl_lc_capability_probe"), "--output", str(test_gcc / "libcurl_consistency" / "reports" / "capabilities.json")],
            "write the capability matrix report in the GCC test tree",
            "test-shared-gcc",
            ("capability_matrix_report",),
        ),
        _step(
            "metadata_scan",
            "full",
            [args.python, "scripts/run_release_gate.py", "--scan-metadata"],
            "scan release metadata without a build-dir fallback",
            "release-shared",
            (),
        ),
        _step(
            "uce_evidence",
            "full",
            [args.python, "scripts/run_uce_gate.py", "--tier", "nightly", "--build-dir", str(test_gcc), "--run-id", "release-gate"],
            "run UCE evidence from the GCC test tree",
            "test-shared-gcc",
            ("uce_report",),
        ),
        _step(
            "doxygen_report",
            "full",
            [
                args.python,
                "scripts/run_doxygen_gate.py",
                "--output-dir",
                str(doxygen_output),
            ],
            "generate and validate the public Doxygen HTML from the release tree",
            "release-shared",
            ("doxygen_report",),
        ),
    ]


def _abi_steps(args: argparse.Namespace) -> list[GateStep]:
    release_shared = tree_path(args, "release-shared")
    if args.abi_mode == "none":
        return []
    if args.abi_mode == "current":
        return [
            _step(
                "abi_current_baseline_diff",
                "full",
                [
                    args.python,
                    "scripts/qcurl_abi_gate.py",
                    "--library",
                    str(release_shared / "src" / "libQCurl.so.2.0.0"),
                    "--headers-dir",
                    "src",
                    "diff",
                    "--report",
                    str(release_shared / "abi" / "qcurl-core-v2.abidiff.txt"),
                    "--current-snapshot",
                    str(release_shared / "abi" / "qcurl-core-v2.current.abi.xml"),
                ],
                "compare the release shared library against the controlled v2 ABI baseline",
                "release-shared",
                ("abi_current_report", "abi_current_snapshot"),
            )
        ]
    if args.abi_mode == "promotion-candidate":
        return [
            _step(
                "abi_hardbreak_report",
                "full",
                [
                    args.python,
                    "scripts/qcurl_abi_gate.py",
                    "--library",
                    str(release_shared / "src" / "libQCurl.so.2.0.0"),
                    "--headers-dir",
                    "src",
                    "hardbreak-report",
                    "--baseline",
                    str(args.abi_hardbreak_baseline),
                    "--report",
                    str(args.abi_hardbreak_report),
                    "--current-snapshot",
                    str(args.abi_hardbreak_current_snapshot),
                ],
                "write the v1-to-v2 hard-break ABI report from the release shared tree",
                "release-shared",
                ("abi_hardbreak_report", "abi_hardbreak_current_snapshot"),
            )
        ]
    raise ValueError(f"unsupported ABI mode: {args.abi_mode}")


def _symbol_steps(args: argparse.Namespace) -> list[GateStep]:
    release_shared = tree_path(args, "release-shared")
    return [
        _step(
            "dynamic_symbol_allowlist",
            "full",
            [args.python, "scripts/qcurl_abi_gate.py", "--library", str(release_shared / "src" / "libQCurl.so.2.0.0"), "--symbol-report", str(release_shared / "abi" / "qcurl-core-v2.dynamic-symbols.json"), "symbols"],
            "validate Core dynamic symbols from the release shared tree",
            "release-shared",
            ("core_dynamic_symbols",),
        ),
        _step(
            "other_extras_dynamic_symbol_allowlist",
            "full",
            [args.python, "scripts/qcurl_abi_gate.py", "--library", str(release_shared / "src" / "libQCurlOtherExtras.so.2.0.0"), "--component", "other-extras", "--symbol-report", str(release_shared / "abi" / "qcurl-other-extras-v2.dynamic-symbols.json"), "symbols"],
            "validate Other Extras dynamic symbols from the release shared tree",
            "release-shared",
            ("other_extras_dynamic_symbols",),
        ),
    ]


def build_steps(args: argparse.Namespace) -> list[GateStep]:
    """返回按 producer tree 固定顺序排列的 release gate 步骤。"""

    if args.tier == "fast":
        return [step for step in _shared_steps(args) if step.tier == "fast"]
    if args.tier == "strict":
        steps = _shared_steps(args)
        steps.extend(_static_package_steps(args))
        steps.extend(_strict_steps(args))
        return [step for step in steps if step.tier in {"fast", "strict"}]
    steps = _shared_steps(args)
    steps.extend(_static_build_steps(args))
    steps.extend(_static_package_steps(args))
    steps.extend(_strict_steps(args))
    steps.extend(_full_test_steps(args))
    steps.extend(_symbol_steps(args))
    steps.extend(_abi_steps(args))
    steps.extend(sanitizer_steps(args))
    steps.extend(_full_evidence_steps(args))
    return steps
