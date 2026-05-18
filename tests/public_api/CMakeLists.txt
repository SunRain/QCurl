find_package(Python3 REQUIRED COMPONENTS Interpreter)

if(NOT DEFINED QCURL_INSTALL_HEADERS OR QCURL_INSTALL_HEADERS STREQUAL "")
    message(FATAL_ERROR "tests/public_api requires QCURL_INSTALL_HEADERS from src/CMakeLists.txt")
endif()

# 该套件审计默认稳定安装面（QCURL_INSTALL_HEADERS + QCurlConfig.h）。
# QCURL_INSTALL_HEADERS_EXTRAS 只包含尚未 promotion 的非默认 Extras，不能混入 manifest。
if(DEFINED QCURL_INSTALL_HEADERS_EXTRAS)
    foreach(extra_header IN LISTS QCURL_INSTALL_HEADERS_EXTRAS)
        list(FIND QCURL_INSTALL_HEADERS "${extra_header}" _qcurl_public_api_extra_index)
        if(NOT _qcurl_public_api_extra_index EQUAL -1)
            message(FATAL_ERROR
                "public-api manifest must not include Extras header: ${extra_header}")
        endif()
    endforeach()
endif()

set(QCURL_PUBLIC_API_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(QCURL_PUBLIC_API_SELF_COMPILE_DIR "${QCURL_PUBLIC_API_GENERATED_DIR}/self_compile")
set(QCURL_PUBLIC_API_MANIFEST "${QCURL_PUBLIC_API_GENERATED_DIR}/public_headers.txt")
set(QCURL_PUBLIC_API_EXTRAS_MANIFEST "${QCURL_PUBLIC_API_GENERATED_DIR}/extras_headers.txt")
set(QCURL_PUBLIC_API_BLOCKING_EXTRAS_MANIFEST
    "${QCURL_PUBLIC_API_GENERATED_DIR}/blocking_extras_headers.txt")
set(QCURL_PUBLIC_API_TEST_SUPPORT_MANIFEST
    "${QCURL_PUBLIC_API_GENERATED_DIR}/test_support_headers.txt")
set(QCURL_PUBLIC_API_OTHER_EXTRAS_MANIFEST
    "${QCURL_PUBLIC_API_GENERATED_DIR}/other_extras_headers.txt")
set(QCURL_PUBLIC_API_SURFACE_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/surface_manifest.json")
set(QCURL_PUBLIC_API_STAGE_DIR "${CMAKE_BINARY_DIR}/public_api/stage")
set(QCURL_PUBLIC_API_BLOCKING_EXTRAS_STAGE_DIR
    "${CMAKE_BINARY_DIR}/public_api/stage_blocking_extras")
set(QCURL_PUBLIC_API_TEST_SUPPORT_STAGE_DIR
    "${CMAKE_BINARY_DIR}/public_api/stage_test_support")
set(QCURL_PUBLIC_API_OTHER_EXTRAS_STAGE_DIR
    "${CMAKE_BINARY_DIR}/public_api/stage_other_extras")
set(QCURL_PUBLIC_API_POSITIVE_BUILD_DIR "${CMAKE_BINARY_DIR}/public_api/consumer_positive")
set(QCURL_PUBLIC_API_NEGATIVE_BUILD_DIR "${CMAKE_BINARY_DIR}/public_api/consumer_negative")
set(QCURL_PUBLIC_API_BLOCKING_EXTRAS_POSITIVE_BUILD_DIR
    "${CMAKE_BINARY_DIR}/public_api/consumer_blocking_extras_positive")
set(QCURL_PUBLIC_API_BLOCKING_EXTRAS_NEGATIVE_BUILD_DIR
    "${CMAKE_BINARY_DIR}/public_api/consumer_blocking_extras_negative")
set(QCURL_PUBLIC_API_TEST_SUPPORT_POSITIVE_BUILD_DIR
    "${CMAKE_BINARY_DIR}/public_api/consumer_test_support_positive")
set(QCURL_PUBLIC_API_TEST_SUPPORT_NEGATIVE_BUILD_DIR
    "${CMAKE_BINARY_DIR}/public_api/consumer_test_support_negative")
set(QCURL_PUBLIC_API_OTHER_EXTRAS_POSITIVE_BUILD_DIR
    "${CMAKE_BINARY_DIR}/public_api/consumer_other_extras_positive")
set(QCURL_PUBLIC_API_OTHER_EXTRAS_NEGATIVE_BUILD_DIR
    "${CMAKE_BINARY_DIR}/public_api/consumer_other_extras_negative")

file(MAKE_DIRECTORY "${QCURL_PUBLIC_API_GENERATED_DIR}")
file(MAKE_DIRECTORY "${QCURL_PUBLIC_API_SELF_COMPILE_DIR}")

string(REPLACE ";" "\n" _qcurl_public_api_header_lines "${QCURL_INSTALL_HEADERS}")
file(GENERATE OUTPUT "${QCURL_PUBLIC_API_MANIFEST}" CONTENT "${_qcurl_public_api_header_lines}\n")
string(REPLACE ";" "\n" _qcurl_public_api_extras_header_lines "${QCURL_INSTALL_HEADERS_EXTRAS}")
file(GENERATE OUTPUT "${QCURL_PUBLIC_API_EXTRAS_MANIFEST}"
    CONTENT "${_qcurl_public_api_extras_header_lines}\n")
string(REPLACE ";" "\n" _qcurl_public_api_blocking_extras_header_lines
    "${QCURL_INSTALL_HEADERS_BLOCKING_EXTRAS}")
file(GENERATE OUTPUT "${QCURL_PUBLIC_API_BLOCKING_EXTRAS_MANIFEST}"
    CONTENT "${_qcurl_public_api_blocking_extras_header_lines}\n")
string(REPLACE ";" "\n" _qcurl_public_api_test_support_header_lines
    "${QCURL_INSTALL_HEADERS_TEST_SUPPORT}")
file(GENERATE OUTPUT "${QCURL_PUBLIC_API_TEST_SUPPORT_MANIFEST}"
    CONTENT "${_qcurl_public_api_test_support_header_lines}\n")
string(REPLACE ";" "\n" _qcurl_public_api_other_extras_header_lines
    "${QCURL_INSTALL_HEADERS_OTHER_EXTRAS}")
file(GENERATE OUTPUT "${QCURL_PUBLIC_API_OTHER_EXTRAS_MANIFEST}"
    CONTENT "${_qcurl_public_api_other_extras_header_lines}\n")

set(_qcurl_public_api_self_compile_sources)
foreach(header IN LISTS QCURL_INSTALL_HEADERS)
    get_filename_component(_qcurl_public_api_stem "${header}" NAME_WE)
    string(MAKE_C_IDENTIFIER "${_qcurl_public_api_stem}" _qcurl_public_api_identifier)
    set(_qcurl_public_api_source
        "${QCURL_PUBLIC_API_SELF_COMPILE_DIR}/${_qcurl_public_api_identifier}.cpp")
    file(GENERATE
        OUTPUT "${_qcurl_public_api_source}"
        CONTENT "#include \"${header}\"\nint qcurl_public_api_${_qcurl_public_api_identifier}() { return 0; }\n")
    list(APPEND _qcurl_public_api_self_compile_sources "${_qcurl_public_api_source}")
endforeach()

add_library(qcurl_public_header_self_compile OBJECT EXCLUDE_FROM_ALL
    ${_qcurl_public_api_self_compile_sources})
target_link_libraries(qcurl_public_header_self_compile
    PRIVATE
        Qt6::Core
        Qt6::Network
)
if(QCURL_WEBSOCKET_SUPPORT)
    target_compile_definitions(qcurl_public_header_self_compile PRIVATE QCURL_WEBSOCKET_SUPPORT)
endif()
if(QCURL_HTTP2_SUPPORT)
    target_compile_definitions(qcurl_public_header_self_compile PRIVATE QCURL_HTTP2_SUPPORT)
endif()
target_include_directories(qcurl_public_header_self_compile
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_BINARY_DIR}/src"
)
add_custom_target(qcurl_public_api_self_compile
    DEPENDS qcurl_public_header_self_compile)

set(_qcurl_public_api_check_script
    "${CMAKE_CURRENT_SOURCE_DIR}/run_public_api_checks.py")

add_test(
    NAME qcurl_public_api_self_compile
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            build-target
            --cmake "${CMAKE_COMMAND}"
            --build-dir "${CMAKE_BINARY_DIR}"
            --target qcurl_public_api_self_compile
            --config "$<CONFIG>"
)
set_tests_properties(qcurl_public_api_self_compile PROPERTIES
    LABELS "public-api"
)

add_test(
    NAME qcurl_public_api_scan
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            scan
            --source-root "${CMAKE_SOURCE_DIR}/src"
            --manifest "${QCURL_PUBLIC_API_MANIFEST}"
)
set_tests_properties(qcurl_public_api_scan PROPERTIES
    LABELS "public-api"
)

add_test(
    NAME qcurl_public_api_hard_break_guards
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            hard-break-guards
            --repo-root "${CMAKE_SOURCE_DIR}"
)
set_tests_properties(qcurl_public_api_hard_break_guards PROPERTIES
    LABELS "public-api"
)

add_test(
    NAME qcurl_public_api_surface_manifest
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            surface-manifest
            --surface-manifest "${QCURL_PUBLIC_API_SURFACE_MANIFEST}"
            --core-manifest "${QCURL_PUBLIC_API_MANIFEST}"
            --extras-manifest "${QCURL_PUBLIC_API_EXTRAS_MANIFEST}"
)
set_tests_properties(qcurl_public_api_surface_manifest PROPERTIES
    LABELS "public-api"
)

add_test(
    NAME qcurl_public_api_stage_install
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            install
            --cmake "${CMAKE_COMMAND}"
            --build-dir "${CMAKE_BINARY_DIR}"
            --stage-dir "${QCURL_PUBLIC_API_STAGE_DIR}"
            --config "$<CONFIG>"
)
set_tests_properties(qcurl_public_api_stage_install PROPERTIES
    LABELS "public-api-slow"
    FIXTURES_SETUP qcurl_public_api_stage
)

add_test(
    NAME qcurl_public_api_blocking_extras_stage_install
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            install
            --cmake "${CMAKE_COMMAND}"
            --build-dir "${CMAKE_BINARY_DIR}"
            --stage-dir "${QCURL_PUBLIC_API_BLOCKING_EXTRAS_STAGE_DIR}"
            --component Development
            --component Runtime
            --component BundledRuntime
            --component BlockingExtrasDevelopment
            --config "$<CONFIG>"
)
set_tests_properties(qcurl_public_api_blocking_extras_stage_install PROPERTIES
    LABELS "public-api-slow"
    FIXTURES_SETUP qcurl_public_api_blocking_extras_stage
)

add_test(
    NAME qcurl_public_api_test_support_stage_install
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            install
            --cmake "${CMAKE_COMMAND}"
            --build-dir "${CMAKE_BINARY_DIR}"
            --stage-dir "${QCURL_PUBLIC_API_TEST_SUPPORT_STAGE_DIR}"
            --component Development
            --component Runtime
            --component BundledRuntime
            --component TestSupportDevelopment
            --config "$<CONFIG>"
)
set_tests_properties(qcurl_public_api_test_support_stage_install PROPERTIES
    LABELS "public-api-slow"
    FIXTURES_SETUP qcurl_public_api_test_support_stage
)

add_test(
    NAME qcurl_public_api_other_extras_stage_install
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            install
            --cmake "${CMAKE_COMMAND}"
            --build-dir "${CMAKE_BINARY_DIR}"
            --stage-dir "${QCURL_PUBLIC_API_OTHER_EXTRAS_STAGE_DIR}"
            --component Development
            --component Runtime
            --component BundledRuntime
            --component OtherExtrasDevelopment
            --config "$<CONFIG>"
)
set_tests_properties(qcurl_public_api_other_extras_stage_install PROPERTIES
    LABELS "public-api-slow"
    FIXTURES_SETUP qcurl_public_api_other_extras_stage
)

add_test(
    NAME qcurl_public_api_install_headers
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            check-installed-headers
            --stage-dir "${QCURL_PUBLIC_API_STAGE_DIR}"
            --manifest "${QCURL_PUBLIC_API_MANIFEST}"
            --generated-header "${QCURL_GENERATED_CONFIG_HEADER}"
)
set_tests_properties(qcurl_public_api_install_headers PROPERTIES
    LABELS "public-api-slow"
    FIXTURES_REQUIRED qcurl_public_api_stage
)

add_test(
    NAME qcurl_public_api_export_contract
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            check-export-contract
            --stage-dir "${QCURL_PUBLIC_API_STAGE_DIR}"
)
set_tests_properties(qcurl_public_api_export_contract PROPERTIES
    LABELS "public-api-slow"
    FIXTURES_REQUIRED qcurl_public_api_stage
)

add_test(
    NAME qcurl_public_api_blocking_extras_install_contract
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            check-blocking-extras-install
            --default-stage-dir "${QCURL_PUBLIC_API_STAGE_DIR}"
            --blocking-stage-dir "${QCURL_PUBLIC_API_BLOCKING_EXTRAS_STAGE_DIR}"
            --manifest "${QCURL_PUBLIC_API_BLOCKING_EXTRAS_MANIFEST}"
)
set_tests_properties(qcurl_public_api_blocking_extras_install_contract PROPERTIES
    LABELS "public-api-slow"
    FIXTURES_REQUIRED "qcurl_public_api_stage;qcurl_public_api_blocking_extras_stage"
)

add_test(
    NAME qcurl_public_api_test_support_install_contract
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            check-test-support-install
            --default-stage-dir "${QCURL_PUBLIC_API_STAGE_DIR}"
            --test-support-stage-dir "${QCURL_PUBLIC_API_TEST_SUPPORT_STAGE_DIR}"
            --manifest "${QCURL_PUBLIC_API_TEST_SUPPORT_MANIFEST}"
)
set_tests_properties(qcurl_public_api_test_support_install_contract PROPERTIES
    LABELS "public-api-slow"
    FIXTURES_REQUIRED "qcurl_public_api_stage;qcurl_public_api_test_support_stage"
)

add_test(
    NAME qcurl_public_api_other_extras_install_contract
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            check-other-extras-install
            --default-stage-dir "${QCURL_PUBLIC_API_STAGE_DIR}"
            --other-extras-stage-dir "${QCURL_PUBLIC_API_OTHER_EXTRAS_STAGE_DIR}"
            --manifest "${QCURL_PUBLIC_API_OTHER_EXTRAS_MANIFEST}"
)
set_tests_properties(qcurl_public_api_other_extras_install_contract PROPERTIES
    LABELS "public-api-slow"
    FIXTURES_REQUIRED "qcurl_public_api_stage;qcurl_public_api_other_extras_stage"
)

add_test(
    NAME qcurl_public_api_consumer_smoke
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            consumer-smoke
            --cmake "${CMAKE_COMMAND}"
            --stage-dir "${QCURL_PUBLIC_API_STAGE_DIR}"
            --positive-source-dir "${CMAKE_CURRENT_SOURCE_DIR}/consumer_smoke"
            --positive-build-dir "${QCURL_PUBLIC_API_POSITIVE_BUILD_DIR}"
            --negative-source-dir "${CMAKE_CURRENT_SOURCE_DIR}/consumer_private_negative"
            --negative-build-dir "${QCURL_PUBLIC_API_NEGATIVE_BUILD_DIR}"
            --config "$<CONFIG>"
)
set_tests_properties(qcurl_public_api_consumer_smoke PROPERTIES
    LABELS "public-api-slow"
    FIXTURES_REQUIRED qcurl_public_api_stage
)

add_test(
    NAME qcurl_public_api_blocking_extras_consumer_smoke
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            blocking-extras-consumer-smoke
            --cmake "${CMAKE_COMMAND}"
            --blocking-stage-dir "${QCURL_PUBLIC_API_BLOCKING_EXTRAS_STAGE_DIR}"
            --default-stage-dir "${QCURL_PUBLIC_API_STAGE_DIR}"
            --positive-source-dir "${CMAKE_CURRENT_SOURCE_DIR}/consumer_blocking_extras_smoke"
            --positive-build-dir "${QCURL_PUBLIC_API_BLOCKING_EXTRAS_POSITIVE_BUILD_DIR}"
            --negative-source-dir "${CMAKE_CURRENT_SOURCE_DIR}/consumer_blocking_extras_negative"
            --negative-build-dir "${QCURL_PUBLIC_API_BLOCKING_EXTRAS_NEGATIVE_BUILD_DIR}"
            --config "$<CONFIG>"
)
set_tests_properties(qcurl_public_api_blocking_extras_consumer_smoke PROPERTIES
    LABELS "public-api-slow"
    FIXTURES_REQUIRED "qcurl_public_api_stage;qcurl_public_api_blocking_extras_stage"
)

add_test(
    NAME qcurl_public_api_test_support_consumer_smoke
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            test-support-consumer-smoke
            --cmake "${CMAKE_COMMAND}"
            --test-support-stage-dir "${QCURL_PUBLIC_API_TEST_SUPPORT_STAGE_DIR}"
            --default-stage-dir "${QCURL_PUBLIC_API_STAGE_DIR}"
            --positive-source-dir "${CMAKE_CURRENT_SOURCE_DIR}/consumer_test_support_smoke"
            --positive-build-dir "${QCURL_PUBLIC_API_TEST_SUPPORT_POSITIVE_BUILD_DIR}"
            --negative-source-dir "${CMAKE_CURRENT_SOURCE_DIR}/consumer_test_support_negative"
            --negative-build-dir "${QCURL_PUBLIC_API_TEST_SUPPORT_NEGATIVE_BUILD_DIR}"
            --config "$<CONFIG>"
)
set_tests_properties(qcurl_public_api_test_support_consumer_smoke PROPERTIES
    LABELS "public-api-slow"
    FIXTURES_REQUIRED "qcurl_public_api_stage;qcurl_public_api_test_support_stage"
)

add_test(
    NAME qcurl_public_api_other_extras_consumer_smoke
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            other-extras-consumer-smoke
            --cmake "${CMAKE_COMMAND}"
            --other-extras-stage-dir "${QCURL_PUBLIC_API_OTHER_EXTRAS_STAGE_DIR}"
            --default-stage-dir "${QCURL_PUBLIC_API_STAGE_DIR}"
            --positive-source-dir "${CMAKE_CURRENT_SOURCE_DIR}/consumer_other_extras_smoke"
            --positive-build-dir "${QCURL_PUBLIC_API_OTHER_EXTRAS_POSITIVE_BUILD_DIR}"
            --negative-source-dir "${CMAKE_CURRENT_SOURCE_DIR}/consumer_other_extras_negative"
            --negative-build-dir "${QCURL_PUBLIC_API_OTHER_EXTRAS_NEGATIVE_BUILD_DIR}"
            --config "$<CONFIG>"
)
set_tests_properties(qcurl_public_api_other_extras_consumer_smoke PROPERTIES
    LABELS "public-api-slow"
    FIXTURES_REQUIRED "qcurl_public_api_stage;qcurl_public_api_other_extras_stage"
)
