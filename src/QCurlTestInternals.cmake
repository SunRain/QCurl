# 白盒测试使用不安装的静态伴随库，避免扩大 shared 库的动态导出面。
if(BUILD_TESTING AND QCURL_BUILD_SHARED_LIBS)
    add_library(QCurlTestInternals STATIC EXCLUDE_FROM_ALL
        ${QCURL_ALL_SOURCES}
        ${QCURL_BLOCKING_EXTRAS_ALL_SOURCES}
        ${QCURL_CORE_HEADERS}
        ${QCURL_INSTALL_HEADERS_BLOCKING_EXTRAS}
    )
    add_library(QCurlOtherExtrasTestInternals STATIC EXCLUDE_FROM_ALL
        ${QCURL_OTHER_EXTRAS_ALL_SOURCES}
        ${QCURL_OTHER_EXTRAS_HEADERS}
    )
    add_library(QCurlTestSupportTestInternals STATIC EXCLUDE_FROM_ALL
        ${QCURL_TEST_SUPPORT_ALL_SOURCES}
        ${QCURL_INSTALL_HEADERS_TEST_SUPPORT}
    )

    foreach(_qcurl_test_internal_target IN ITEMS
            QCurlTestInternals
            QCurlTestSupportTestInternals
            QCurlOtherExtrasTestInternals)
        qcurl_enable_qt_no_keywords(${_qcurl_test_internal_target})
        set_target_properties(${_qcurl_test_internal_target} PROPERTIES
            AUTOMOC ON
            POSITION_INDEPENDENT_CODE ON
        )
        target_compile_definitions(${_qcurl_test_internal_target}
            PRIVATE
                QCURL_ENABLE_TEST_HOOKS
                QT_NO_CAST_FROM_ASCII
                QT_NO_CAST_TO_ASCII
                QT_NO_URL_CAST_FROM_STRING
        )
    endforeach()

    target_link_libraries(QCurlTestInternals PUBLIC Qt6::Core)
    target_include_directories(QCurlTestInternals
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}
            ${CMAKE_BINARY_DIR}/src
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/private
    )
    target_compile_definitions(QCurlTestInternals PUBLIC QCURL_STATIC_DEFINE)
    target_compile_definitions(QCurlTestInternals
        PRIVATE QCURL_ENABLE_ADVANCED_REQUEST_NETWORK_PATH_API)

    target_link_libraries(QCurlTestSupportTestInternals
        PUBLIC QCurlTestInternals)
    target_include_directories(QCurlTestSupportTestInternals
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}
            ${CMAKE_BINARY_DIR}/src
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/private
    )
    target_link_libraries(QCurlOtherExtrasTestInternals
        PUBLIC
            QCurlTestInternals
            Qt6::Network
    )
    target_include_directories(QCurlOtherExtrasTestInternals
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}
            ${CMAKE_BINARY_DIR}/src
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/private
    )
    target_compile_definitions(QCurlOtherExtrasTestInternals
        PUBLIC
            QCURL_OTHER_EXTRAS_STATIC_DEFINE
    )

    if(QCURL_WEBSOCKET_SUPPORT)
        target_compile_definitions(QCurlTestInternals PUBLIC QCURL_WEBSOCKET_SUPPORT)
        target_compile_definitions(QCurlOtherExtrasTestInternals PUBLIC QCURL_WEBSOCKET_SUPPORT)
    endif()
    if(QCURL_HTTP2_SUPPORT)
        target_compile_definitions(QCurlTestInternals PUBLIC QCURL_HTTP2_SUPPORT)
        target_compile_definitions(QCurlOtherExtrasTestInternals PUBLIC QCURL_HTTP2_SUPPORT)
    endif()

    if(QCURL_BUILD_LIBCURL_CONSISTENCY)
        foreach(_qcurl_test_internal_target IN ITEMS
                QCurlTestInternals
                QCurlOtherExtrasTestInternals)
            target_include_directories(${_qcurl_test_internal_target}
                PRIVATE "${CMAKE_SOURCE_DIR}/curl/include")
            target_link_libraries(${_qcurl_test_internal_target}
                PRIVATE "$<BUILD_INTERFACE:$<TARGET_FILE:libcurl_shared>>")
            add_dependencies(${_qcurl_test_internal_target} libcurl_shared)
        endforeach()
    else()
        target_link_libraries(QCurlTestInternals PUBLIC CURL::libcurl)
        target_link_libraries(QCurlOtherExtrasTestInternals PUBLIC CURL::libcurl)
    endif()
endif()
