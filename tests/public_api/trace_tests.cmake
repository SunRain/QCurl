set(QCURL_TRACE_CONSUMER_BUILD_DIR "${CMAKE_BINARY_DIR}/public_api/consumer_trace")
set(_trace_consumer_script "${CMAKE_CURRENT_SOURCE_DIR}/trace_consumer.py")
add_test(NAME qcurl_public_api_trace_consumer_build
    COMMAND "${Python3_EXECUTABLE}" "${_trace_consumer_script}" build
        --cmake "${CMAKE_COMMAND}" --consumer-cache "${QCURL_CONSUMER_PROFILE}"
        --stage-dir "${QCURL_PUBLIC_API_PACKAGE_STAGE_DIR}"
        --build-dir "${QCURL_TRACE_CONSUMER_BUILD_DIR}" --config "$<CONFIG>")
set_tests_properties(qcurl_public_api_trace_consumer_build PROPERTIES
    LABELS "public-api-slow;diagnostic-trace"
    FIXTURES_REQUIRED qcurl_public_api_package_stage
    FIXTURES_SETUP qcurl_trace_consumer)
foreach(_trace_protocol IN ITEMS h1 h2)
    foreach(_trace_ids IN ITEMS on off)
        set(_trace_test "qcurl_trace_installed_${_trace_protocol}_ids_${_trace_ids}")
        add_test(NAME "${_trace_test}"
            COMMAND "${Python3_EXECUTABLE}" "${_trace_consumer_script}" run
                --stage-dir "${QCURL_PUBLIC_API_PACKAGE_STAGE_DIR}"
                --build-dir "${QCURL_TRACE_CONSUMER_BUILD_DIR}" --config "$<CONFIG>"
                --protocol "${_trace_protocol}" --ids "${_trace_ids}"
                --http2-probe-bin "$<TARGET_FILE:qcurl_http2_capability_probe>")
        set_tests_properties("${_trace_test}" PROPERTIES
            LABELS "public-api-slow;env;local_port;diagnostic-trace"
            ENVIRONMENT "QT_LOGGING_RULES=*.debug=false;*.info=true"
            FIXTURES_REQUIRED qcurl_trace_consumer
            FAIL_REGULAR_EXPRESSION "SKIP *:")
    endforeach()
endforeach()
