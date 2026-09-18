add_test(
    NAME qcurl_public_api_consumer_smoke
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            consumer-smoke
            --cmake "${CMAKE_COMMAND}"
            --consumer-cache "${QCURL_PUBLIC_API_CONSUMER_CACHE}"
            --stage-dir "${QCURL_PUBLIC_API_PACKAGE_STAGE_DIR}"
            --positive-source-dir "${CMAKE_CURRENT_SOURCE_DIR}/consumer_smoke"
            --positive-build-dir "${QCURL_PUBLIC_API_POSITIVE_BUILD_DIR}"
            --negative-source-dir "${CMAKE_CURRENT_SOURCE_DIR}/consumer_private_negative"
            --negative-build-dir "${QCURL_PUBLIC_API_NEGATIVE_BUILD_DIR}"
            --config "$<CONFIG>"
)
set_tests_properties(qcurl_public_api_consumer_smoke PROPERTIES
    LABELS "public-api-slow"
    FIXTURES_REQUIRED qcurl_public_api_package_stage
)

add_test(
    NAME qcurl_public_api_metatype_consumer_smoke
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            metatype-consumer-smoke
            --cmake "${CMAKE_COMMAND}"
            --consumer-cache "${QCURL_PUBLIC_API_CONSUMER_CACHE}"
            --stage-dir "${QCURL_PUBLIC_API_PACKAGE_STAGE_DIR}"
            --source-dir "${CMAKE_CURRENT_SOURCE_DIR}/consumer_metatype_smoke"
            --build-dir "${QCURL_PUBLIC_API_METATYPE_BUILD_DIR}"
            --config "$<CONFIG>"
)
set_tests_properties(qcurl_public_api_metatype_consumer_smoke PROPERTIES
    LABELS "public-api-slow"
    FIXTURES_REQUIRED qcurl_public_api_package_stage
)

add_test(
    NAME qcurl_public_api_blocking_extras_consumer_smoke
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            blocking-extras-consumer-smoke
            --cmake "${CMAKE_COMMAND}"
            --consumer-cache "${QCURL_PUBLIC_API_CONSUMER_CACHE}"
            --blocking-stage-dir "${QCURL_PUBLIC_API_PACKAGE_STAGE_DIR}"
            --default-stage-dir "${QCURL_PUBLIC_API_STAGE_DIR}"
            --positive-source-dir "${CMAKE_CURRENT_SOURCE_DIR}/consumer_blocking_extras_smoke"
            --positive-build-dir "${QCURL_PUBLIC_API_BLOCKING_EXTRAS_POSITIVE_BUILD_DIR}"
            --negative-source-dir "${CMAKE_CURRENT_SOURCE_DIR}/consumer_blocking_extras_negative"
            --negative-build-dir "${QCURL_PUBLIC_API_BLOCKING_EXTRAS_NEGATIVE_BUILD_DIR}"
            --config "$<CONFIG>"
)
set_tests_properties(qcurl_public_api_blocking_extras_consumer_smoke PROPERTIES
    LABELS "public-api-slow"
    FIXTURES_REQUIRED "qcurl_public_api_stage;qcurl_public_api_package_stage"
)

add_test(
    NAME qcurl_public_api_test_support_consumer_smoke
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            test-support-consumer-smoke
            --cmake "${CMAKE_COMMAND}"
            --consumer-cache "${QCURL_PUBLIC_API_CONSUMER_CACHE}"
            --test-support-stage-dir "${QCURL_PUBLIC_API_PACKAGE_STAGE_DIR}"
            --default-stage-dir "${QCURL_PUBLIC_API_STAGE_DIR}"
            --positive-source-dir "${CMAKE_CURRENT_SOURCE_DIR}/consumer_test_support_smoke"
            --positive-build-dir "${QCURL_PUBLIC_API_TEST_SUPPORT_POSITIVE_BUILD_DIR}"
            --negative-source-dir "${CMAKE_CURRENT_SOURCE_DIR}/consumer_test_support_negative"
            --negative-build-dir "${QCURL_PUBLIC_API_TEST_SUPPORT_NEGATIVE_BUILD_DIR}"
            --config "$<CONFIG>"
)
set_tests_properties(qcurl_public_api_test_support_consumer_smoke PROPERTIES
    LABELS "public-api-slow"
    FIXTURES_REQUIRED "qcurl_public_api_stage;qcurl_public_api_package_stage"
)

add_test(
    NAME qcurl_public_api_other_extras_consumer_smoke
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            other-extras-consumer-smoke
            --cmake "${CMAKE_COMMAND}"
            --consumer-cache "${QCURL_PUBLIC_API_CONSUMER_CACHE}"
            --other-extras-stage-dir "${QCURL_PUBLIC_API_PACKAGE_STAGE_DIR}"
            --default-stage-dir "${QCURL_PUBLIC_API_STAGE_DIR}"
            --positive-source-dir "${CMAKE_CURRENT_SOURCE_DIR}/consumer_other_extras_smoke"
            --positive-build-dir "${QCURL_PUBLIC_API_OTHER_EXTRAS_POSITIVE_BUILD_DIR}"
            --negative-source-dir "${CMAKE_CURRENT_SOURCE_DIR}/consumer_other_extras_negative"
            --negative-build-dir "${QCURL_PUBLIC_API_OTHER_EXTRAS_NEGATIVE_BUILD_DIR}"
            --config "$<CONFIG>"
)
set_tests_properties(qcurl_public_api_other_extras_consumer_smoke PROPERTIES
    LABELS "public-api-slow"
    FIXTURES_REQUIRED "qcurl_public_api_stage;qcurl_public_api_package_stage"
)

add_test(
    NAME qcurl_public_api_magic_hard_break_negative_diagnostics_defaults
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            hard-break-negative-consumer
            --cmake "${CMAKE_COMMAND}"
            --consumer-cache "${QCURL_PUBLIC_API_CONSUMER_CACHE}"
            --stage-dir "${QCURL_PUBLIC_API_OTHER_EXTRAS_STAGE_DIR}"
            --source-dir "${CMAKE_CURRENT_SOURCE_DIR}/consumer_magic_hard_break_negative_diagnostics_defaults"
            --build-dir "${QCURL_PUBLIC_API_MAGIC_HARD_BREAK_NEGATIVE_DIAGNOSTICS_BUILD_DIR}"
            --config "$<CONFIG>"
)
set_tests_properties(qcurl_public_api_magic_hard_break_negative_diagnostics_defaults PROPERTIES
    LABELS "public-api-slow"
    FIXTURES_REQUIRED qcurl_public_api_other_extras_stage
)

add_test(
    NAME qcurl_public_api_magic_hard_break_negative_websocket_mutators
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            hard-break-negative-consumer
            --cmake "${CMAKE_COMMAND}"
            --consumer-cache "${QCURL_PUBLIC_API_CONSUMER_CACHE}"
            --stage-dir "${QCURL_PUBLIC_API_OTHER_EXTRAS_STAGE_DIR}"
            --source-dir "${CMAKE_CURRENT_SOURCE_DIR}/consumer_magic_hard_break_negative_websocket_mutators"
            --build-dir "${QCURL_PUBLIC_API_MAGIC_HARD_BREAK_NEGATIVE_WEBSOCKET_MUTATORS_BUILD_DIR}"
            --config "$<CONFIG>"
)
set_tests_properties(qcurl_public_api_magic_hard_break_negative_websocket_mutators PROPERTIES
    LABELS "public-api-slow"
    FIXTURES_REQUIRED qcurl_public_api_other_extras_stage
)

add_test(
    NAME qcurl_public_api_magic_hard_break_negative_reconnect_int_codes
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            hard-break-negative-consumer
            --cmake "${CMAKE_COMMAND}"
            --consumer-cache "${QCURL_PUBLIC_API_CONSUMER_CACHE}"
            --stage-dir "${QCURL_PUBLIC_API_OTHER_EXTRAS_STAGE_DIR}"
            --source-dir "${CMAKE_CURRENT_SOURCE_DIR}/consumer_magic_hard_break_negative_reconnect_int_codes"
            --build-dir "${QCURL_PUBLIC_API_MAGIC_HARD_BREAK_NEGATIVE_RECONNECT_CODES_BUILD_DIR}"
            --config "$<CONFIG>"
)
set_tests_properties(qcurl_public_api_magic_hard_break_negative_reconnect_int_codes PROPERTIES
    LABELS "public-api-slow"
    FIXTURES_REQUIRED qcurl_public_api_other_extras_stage
)

add_test(
    NAME qcurl_public_api_magic_hard_break_negative_close_received_typed
    COMMAND "${Python3_EXECUTABLE}" "${_qcurl_public_api_check_script}"
            hard-break-negative-consumer
            --cmake "${CMAKE_COMMAND}"
            --consumer-cache "${QCURL_PUBLIC_API_CONSUMER_CACHE}"
            --stage-dir "${QCURL_PUBLIC_API_OTHER_EXTRAS_STAGE_DIR}"
            --source-dir "${CMAKE_CURRENT_SOURCE_DIR}/consumer_magic_hard_break_negative_close_received_typed"
            --build-dir "${QCURL_PUBLIC_API_MAGIC_HARD_BREAK_NEGATIVE_CLOSE_SIGNAL_BUILD_DIR}"
            --config "$<CONFIG>"
)
set_tests_properties(qcurl_public_api_magic_hard_break_negative_close_received_typed PROPERTIES
    LABELS "public-api-slow"
    FIXTURES_REQUIRED qcurl_public_api_other_extras_stage
)
