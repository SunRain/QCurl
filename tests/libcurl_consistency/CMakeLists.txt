# QCurl ↔ libcurl 可观测一致性门禁（构建依赖）
#
# 注意：
# - 该目录下的 pytest suite 是运行时门禁；这里仅负责“可构建的依赖目标”。
# - QtTest 执行器 `tst_LibcurlConsistency` 由 pytest driver 调用，不注册为 ctest（避免 QSKIP=0 被误判为 Passed）。

if(QCURL_BUILD_LIBCURL_CONSISTENCY)
    # pytest 驱动：Qt Test 执行器（不注册为 ctest）
    #
    # 背景：
    # - 该二进制通过环境变量（QCURL_LC_CASE_ID 等）选择具体 case，并由 pytest driver 调用生成 artifacts。
    # - 若直接作为 ctest 测试运行，在缺少 env 时会 QSKIP 且进程返回 0，ctest 会将其计为 Passed，
    #   容易造成“全绿但未覆盖”的误读。
    add_executable(tst_LibcurlConsistency tst_LibcurlConsistency.cpp)
    target_link_libraries(tst_LibcurlConsistency
        PRIVATE
            Qt6::Test
            QCurl
            CURL::libcurl
    )
    target_compile_definitions(tst_LibcurlConsistency PRIVATE QCURL_ENABLE_TEST_HOOKS)
    target_include_directories(tst_LibcurlConsistency
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_BINARY_DIR}/src
    )
    set_target_properties(tst_LibcurlConsistency PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )

    # baseline clients（非 Qt Test，供 pytest suite 使用）
    add_executable(qcurl_lc_ws_baseline ws_baseline_client.cpp)
    target_link_libraries(qcurl_lc_ws_baseline PRIVATE CURL::libcurl)
    set_target_properties(qcurl_lc_ws_baseline PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )

    add_executable(qcurl_lc_range_resume_baseline range_resume_baseline_client.cpp)
    target_link_libraries(qcurl_lc_range_resume_baseline PRIVATE CURL::libcurl)
    set_target_properties(qcurl_lc_range_resume_baseline PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )

    add_executable(qcurl_lc_postfields_binary_baseline postfields_binary_baseline_client.cpp)
    target_link_libraries(qcurl_lc_postfields_binary_baseline PRIVATE CURL::libcurl)
    set_target_properties(qcurl_lc_postfields_binary_baseline PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )

    add_executable(qcurl_lc_multi_get4_baseline multi_get4_baseline_client.cpp)
    target_link_libraries(qcurl_lc_multi_get4_baseline PRIVATE CURL::libcurl)
    set_target_properties(qcurl_lc_multi_get4_baseline PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )

    add_executable(qcurl_lc_http_baseline http_baseline_client.cpp)
    target_link_libraries(qcurl_lc_http_baseline PRIVATE CURL::libcurl)
    set_target_properties(qcurl_lc_http_baseline PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )

    add_executable(qcurl_lc_pause_resume_baseline pause_resume_baseline_client.cpp)
    target_link_libraries(qcurl_lc_pause_resume_baseline PRIVATE CURL::libcurl)
    set_target_properties(qcurl_lc_pause_resume_baseline PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )

    add_executable(qcurl_lc_backpressure_baseline backpressure_baseline_client.cpp)
    target_link_libraries(qcurl_lc_backpressure_baseline PRIVATE CURL::libcurl)
    set_target_properties(qcurl_lc_backpressure_baseline PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )

    add_executable(qcurl_lc_upload_pause_resume_baseline upload_pause_resume_baseline_client.cpp)
    target_link_libraries(qcurl_lc_upload_pause_resume_baseline PRIVATE CURL::libcurl)
    set_target_properties(qcurl_lc_upload_pause_resume_baseline PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )

    # h3-capable nghttpx（best-effort：不作为强依赖；失败将导致 h3 变体 skip，并由 gate 输出告警）
    add_subdirectory(nghttpx_from_source)

    # 上游 curl/tests 最小依赖集合（随用例演进而维护）
    add_custom_target(qcurl_lc_upstream_testdeps)
    if(TARGET testdeps)
        add_dependencies(qcurl_lc_upstream_testdeps testdeps)
    endif()
    if(TARGET curl)
        add_dependencies(qcurl_lc_upstream_testdeps curl)
    endif()

    # Gate 构建入口（run_gate.py --build 只需构建此 target）
    add_custom_target(qcurl_lc_deps)
    add_dependencies(qcurl_lc_deps
        qcurl_lc_upstream_testdeps
        tst_LibcurlConsistency
        qcurl_lc_range_resume_baseline
        qcurl_lc_postfields_binary_baseline
        qcurl_lc_http_baseline
        qcurl_lc_pause_resume_baseline
        qcurl_lc_backpressure_baseline
        qcurl_lc_upload_pause_resume_baseline
        qcurl_lc_multi_get4_baseline
        qcurl_lc_ws_baseline
    )
endif()
