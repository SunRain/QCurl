# 每个 lib-ids 模式均为独立进程，保持既有 preflight 与 QtTest 零 skip 约束。
foreach(_trace_suite IN ITEMS BuiltinLogRedaction Http2)
    foreach(_trace_mode IN ITEMS On Off)
        string(TOLOWER "${_trace_mode}" _trace_value)
        set(_trace_name "tst_QCNetwork${_trace_suite}LibIds${_trace_mode}")
        set(_trace_preflight --require-local-port)
        if(_trace_suite STREQUAL "Http2")
            list(APPEND _trace_preflight --require-http2-suite
                --http2-probe-bin "$<TARGET_FILE:qcurl_http2_capability_probe>")
        endif()
        add_test(NAME "${_trace_name}"
            COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/qcurl/run_qttest_with_preflight.py"
                --test-bin "$<TARGET_FILE:tst_QCNetwork${_trace_suite}>"
                --test-name "${_trace_name}" ${_trace_preflight})
        set_tests_properties("${_trace_name}" PROPERTIES
            ENVIRONMENT "QCURL_TEST_LIB_IDS=${_trace_value}"
            LABELS "env;local_port;diagnostic-trace"
            FAIL_REGULAR_EXPRESSION "SKIP *:")
    endforeach()
endforeach()
