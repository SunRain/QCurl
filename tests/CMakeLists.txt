# QCurl tests
#
# 目录结构：
# - tests/qcurl/：QCurl 本身的 QtTest（C++）与本地依赖
# - tests/public_api/：public header/install/export/consumer 边界门禁
# - tests/libcurl_consistency/：QCurl ↔ libcurl 一致性门禁（pytest + baseline clients + gate deps）

find_package(Qt6 REQUIRED COMPONENTS Test)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

enable_testing()
set(CMAKE_AUTOMOC ON)

add_test(
    NAME qcurl_static_testing_unsupported_configure
    COMMAND "${CMAKE_COMMAND}"
            "-DQCURL_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
            "-DQCURL_TEST_BINARY_ROOT=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/qcurl_static_testing_unsupported.cmake"
)
set_tests_properties(qcurl_static_testing_unsupported_configure PROPERTIES
    LABELS "cmake;negative;offline"
)

add_subdirectory(public_api)
add_subdirectory(qcurl)
add_subdirectory(libcurl_consistency)

# 该入口不得加载 curl testenv；它独立保护一致性门禁自身的解析与证据合同。
add_test(
    NAME qcurl_libcurl_consistency_infrastructure
    COMMAND "${Python3_EXECUTABLE}" -m pytest --noconftest -q
            "${CMAKE_SOURCE_DIR}/tests/libcurl_consistency/test_compare_unit.py"
            "${CMAKE_SOURCE_DIR}/tests/libcurl_consistency/test_run_gate_unit.py"
            "${CMAKE_SOURCE_DIR}/tests/libcurl_consistency/test_minimal_set_consistency.py"
            "${CMAKE_SOURCE_DIR}/tests/test_coverage_maps.py"
            "${CMAKE_SOURCE_DIR}/tests/infrastructure/test_gate_execution_manifest.py"
            "${CMAKE_SOURCE_DIR}/tests/infrastructure/test_gate_evidence_integrity.py"
            "${CMAKE_SOURCE_DIR}/tests/infrastructure/test_capability_manifest.py"
            "${CMAKE_SOURCE_DIR}/tests/infrastructure/test_consistency_workflows.py"
            "${CMAKE_SOURCE_DIR}/tests/infrastructure/test_contract_maps.py"
            "${CMAKE_SOURCE_DIR}/tests/infrastructure/test_run_gate_scoping.py"
)
set_tests_properties(qcurl_libcurl_consistency_infrastructure PROPERTIES
    LABELS "offline;source_guard;libcurl_consistency;infrastructure"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# UCE 自身的负向合同必须在 PR/nightly 的 offline 路线上自动阻断。
add_test(
    NAME qcurl_uce_infrastructure
    COMMAND "${Python3_EXECUTABLE}" -m pytest --noconftest -q
            "${CMAKE_SOURCE_DIR}/tests/test_run_uce_gate.py"
            "${CMAKE_SOURCE_DIR}/tests/test_uce_acceptance_contract.py"
            "${CMAKE_SOURCE_DIR}/tests/test_uce_archive_validation.py"
            "${CMAKE_SOURCE_DIR}/tests/test_uce_candidate.py"
            "${CMAKE_SOURCE_DIR}/tests/test_ci_repository_inputs.py"
            "${CMAKE_SOURCE_DIR}/tests/test_uce_consistency_acceptance.py"
            "${CMAKE_SOURCE_DIR}/tests/test_uce_ctest_gates.py"
            "${CMAKE_SOURCE_DIR}/tests/test_uce_httpbin_contract.py"
            "${CMAKE_SOURCE_DIR}/tests/test_uce_failure_evidence.py"
            "${CMAKE_SOURCE_DIR}/tests/test_uce_manifest.py"
            "${CMAKE_SOURCE_DIR}/tests/test_netproof_capabilities.py"
            "${CMAKE_SOURCE_DIR}/tests/test_netproof_strace_gate.py"
            "${CMAKE_SOURCE_DIR}/tests/test_run_uce_sanitizers.py"
            "${CMAKE_SOURCE_DIR}/tests/test_uce_ctbp.py"
            "${CMAKE_SOURCE_DIR}/tests/test_uce_ctbp_legacy_only_negative.py"
            "${CMAKE_SOURCE_DIR}/tests/test_uce_hes.py"
            "${CMAKE_SOURCE_DIR}/tests/test_uce_bp.py"
            "${CMAKE_SOURCE_DIR}/tests/test_uce_timeline.py"
            "${CMAKE_SOURCE_DIR}/tests/test_uce_redaction_gate.py"
            "${CMAKE_SOURCE_DIR}/tests/test_policy_violations_dictionary_scan.py"
            "${CMAKE_SOURCE_DIR}/tests/infrastructure/test_uce_libcurl_scoping.py"
)
set_tests_properties(qcurl_uce_infrastructure PROPERTIES
    LABELS "offline;source_guard;uce;infrastructure"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

add_test(
    NAME qcurl_deprecated_curl_api_guard
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/scripts/check_deprecated_curl_apis.py"
            --curl-header "${CMAKE_SOURCE_DIR}/curl/include/curl/curl.h"
            --scan-root "${CMAKE_SOURCE_DIR}/src"
            --scan-root "${CMAKE_SOURCE_DIR}/tests"
)
set_tests_properties(qcurl_deprecated_curl_api_guard PROPERTIES
    LABELS "offline;source_guard"
)

add_test(
    NAME qcurl_skip_contract_guard
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/scripts/check_skip_contract.py"
            --cmake
            "${CMAKE_SOURCE_DIR}/tests/qcurl/CMakeLists.txt"
)
set_tests_properties(qcurl_skip_contract_guard PROPERTIES
    LABELS "offline;source_guard"
)

add_test(
    NAME qcurl_label_matrix_guard
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/scripts/check_qcurl_label_matrix.py"
            --cmake
            "${CMAKE_SOURCE_DIR}/tests/qcurl/CMakeLists.txt"
)
set_tests_properties(qcurl_label_matrix_guard PROPERTIES
    LABELS "offline;source_guard"
)

add_test(
    NAME qcurl_release_contract_guard
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/scripts/check_release_contract.py"
            --source-root
            "${CMAKE_SOURCE_DIR}"
)
set_tests_properties(qcurl_release_contract_guard PROPERTIES
    LABELS "offline;source_guard"
)

add_test(
    NAME qcurl_libcurl_binding_contract_guard
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/scripts/check_libcurl_binding_contract.py"
            --source-root
            "${CMAKE_SOURCE_DIR}/src"
)
set_tests_properties(qcurl_libcurl_binding_contract_guard PROPERTIES
    LABELS "offline;source_guard"
)

add_test(
    NAME qcurl_qt_member_naming_guard
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/scripts/check_qt_member_naming.py"
            --source-root "${CMAKE_SOURCE_DIR}"
            --compile-commands "${CMAKE_BINARY_DIR}"
)
set_tests_properties(qcurl_qt_member_naming_guard PROPERTIES
    LABELS "offline;source_guard"
)

add_test(
    NAME qcurl_qt_contract_guard
    COMMAND "${Python3_EXECUTABLE}" -m pytest -q
            "${CMAKE_SOURCE_DIR}/tests/test_qt_contract_guards.py"
)
set_tests_properties(qcurl_qt_contract_guard PROPERTIES
    LABELS "offline;source_guard"
)
