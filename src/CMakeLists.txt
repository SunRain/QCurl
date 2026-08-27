# QCurl 库源文件配置

set(QCURL_CORE_SOURCES
    QCGlobal.cpp
    QCurlRuntime.cpp
    private/QCurlRuntimeLease.cpp
    private/QCurlRuntimeState.cpp
    private/QCBlockingHandleBridge.cpp
    private/QCNetworkMockProvider.cpp
    QCCookie.cpp
    QCCookieAsyncResult.cpp
    QCNetworkAccessManager.cpp
    QCNetworkAccessManagerConfig.cpp
    QCNetworkAccessManagerCookies.cpp
    QCNetworkAccessManagerSend.cpp
    QCNetworkAccessManagerScheduler.cpp
    QCNetworkAccessManagerServices.cpp
    QCNetworkTestSupport.cpp
    QCNetworkHttpAuthConfig.cpp
    QCNetworkLaneKey.cpp
    QCNetworkLaneCancelResult.cpp
    QCNetworkSchedulerPolicy.cpp
    QCNetworkRequest.cpp
    QCNetworkRequestTransferAccessors.cpp
    QCNetworkRedirectConfig.cpp
    QCNetworkTransferConfig.cpp
    QCUtility.cpp
)

set(QCURL_CORE_HEADERS
    QCurlRuntime.h
    private/QCurlRuntimeState_p.h
    QCBlockingCookieStore.h
    QCCookie.h
    QCCookieAsyncResult.h
    QCNetworkAccessManager.h
    QCNetworkAccessManager_p.h
    QCNetworkRequestConfig.h
    QCNetworkRequest.h
    private/QCNetworkRequestPrivate_p.h
    QCUtility.h
    qbytedata_p.h
)

set(QCURL_BLOCKING_EXTRAS_SOURCES
    QCBlockingCookieStore.cpp
    QCBlockingNetworkClient.cpp
    QCBlockingNetworkClientOptions.cpp
    QCBlockingNetworkResult.cpp
    private/QCBlockingCurlAdapter.cpp
    private/QCBlockingCurlCookieSetup.cpp
    private/QCBlockingCurlMethodSetup.cpp
    private/QCBlockingCurlProtocolSetup.cpp
    private/QCBlockingResponseSink.cpp
    private/QCBlockingCurlRequestSetup.cpp
    private/QCBlockingCurlRequestSetupExtensions.cpp
    private/QCBlockingRequestBody.cpp
)

set(QCURL_TEST_SUPPORT_SOURCES)

set(QCURL_OTHER_EXTRAS_SOURCES
    QCNetworkLogRedaction.cpp       # Other Extras 复用的隐藏脱敏实现
    QCNetworkDiagnostics.cpp        # Other Extras / Preview: DNS 诊断工具
    QCNetworkDiagnosticsConnectivity.cpp # Other Extras / Preview: 连接、SSL、HTTP 诊断工具
    QCNetworkDiagnosticsProcess.cpp # Other Extras / Preview: 外部命令诊断工具
    QCNetworkDiagnosticsValues.cpp  # Other Extras / Preview: 诊断结果值类型
    private/QCNetworkDiagnosticsOperation.cpp # Diagnostics 异步完成状态
    QCNetworkMiddlewareExtras.cpp   # Middleware Extras opt-in 实现
)

include("${CMAKE_CURRENT_LIST_DIR}/QCurlFeatureSources.cmake")

set(QCURL_WEBSOCKET_PREVIEW_SOURCES
    QCWebSocketCommandResult.cpp
    QCWebSocketOptions.cpp
    QCWebSocket.cpp
    private/QCWebSocketLifecycle.cpp
    private/QCWebSocketTeardown.cpp
    private/QCWebSocketFrames.cpp
    private/QCWebSocketFrameAssembler.cpp
    private/QCWebSocketSendQueue.cpp
    private/QCWebSocketReconnect.cpp
    QCWebSocketReconnectPolicy.cpp
    QCWebSocketPool.cpp                 # Other Extras / Preview: WebSocket 连接池
    QCWebSocketPoolMutators.cpp         # Other Extras / Preview: WebSocket 连接池同步变更
    QCWebSocketPoolMaintenance.cpp      # Other Extras / Preview: WebSocket 连接池维护逻辑
    QCWebSocketPoolValues.cpp           # Other Extras / Preview: WebSocket 连接池值类型
    private/QCWebSocketCloseCode.cpp
    private/QCWebSocketCurlOptions.cpp
)

# ============================================================================
# WebSocket 支持（Other Extras / Preview）+ 重连机制
# ============================================================================
if(QCURL_WEBSOCKET_SUPPORT)
    list(APPEND QCURL_OTHER_EXTRAS_SOURCES ${QCURL_WEBSOCKET_PREVIEW_SOURCES})
    list(APPEND QCURL_FEATURE_HEADERS
        QCWebSocketCommandResult.h
        QCWebSocket.h
        QCWebSocketReconnectPolicy.h
        QCWebSocketPool.h                   # Other Extras / Preview: WebSocket 连接池
        QCWebSocketPoolResults.h
    )
    list(APPEND QCURL_FEATURE_HEADERS
        QCWebSocket_p.h
        private/QCWebSocketCloseCode_p.h
        private/QCWebSocketCurlOptions_p.h
        private/QCWebSocketFrameAssembler_p.h
        private/QCWebSocketSendQueue_p.h
    )
    message(STATUS "✅ WebSocket 支持已启用（libcurl ${QCURL_LIBCURL_VERSION_STRING} + Other Extras / Preview）")
else()
    message(STATUS "⏭️ WebSocket 支持未启用（需要 libcurl 7.86.0+）")
endif()

# 合并所有源文件和头文件
set(QCURL_ALL_SOURCES
    ${QCURL_CORE_SOURCES}
    ${QCURL_FEATURE_SOURCES}
)
set(QCURL_BLOCKING_EXTRAS_ALL_SOURCES ${QCURL_BLOCKING_EXTRAS_SOURCES})
set(QCURL_TEST_SUPPORT_ALL_SOURCES ${QCURL_TEST_SUPPORT_SOURCES})
set(QCURL_OTHER_EXTRAS_ALL_SOURCES ${QCURL_OTHER_EXTRAS_SOURCES})
set(QCURL_ALL_HEADERS ${QCURL_CORE_HEADERS} ${QCURL_FEATURE_HEADERS})
list(APPEND QCURL_CORE_HEADERS QCNetworkRequestPriority.h)
set(QCURL_OTHER_EXTRAS_HEADERS ${QCURL_INSTALL_HEADERS_OTHER_EXTRAS})
foreach(_qcurl_header IN LISTS QCURL_FEATURE_HEADERS)
    if(_qcurl_header MATCHES "^QCWebSocket")
        list(APPEND QCURL_OTHER_EXTRAS_HEADERS "${_qcurl_header}")
    endif()
endforeach()
# 默认稳定安装面（public package SSOT）只包含 Core contract：
# - 仅 `QCURL_INSTALL_HEADERS + QCurlConfig.h` 进入默认安装前缀
# - `QCNetworkLogger.h` 因 `QCNetworkAccessManager` 的对外接口依赖而进入 Core
# - `QCNetworkCachePolicy.h` 因 `QCNetworkRequest` 的对外接口依赖而进入 Core
# - `QCNetworkDefaultLogger.h` / `QCNetworkCancelToken.h` 作为 P1 低风险 Core helper 进入默认安装面
# - Cache concrete headers 完成 accessor/pimpl 重塑后作为 P2 Core 能力进入默认安装面
# - `QCMultipartFormData.h` 完成 shared-data 重塑后作为 P2 Core builder 进入默认安装面
# - ConnectionPool config / statistics 完成 accessor/shared-data 重塑后作为 P3 Core 管理面进入默认安装面
set(QCURL_INSTALL_HEADERS
    QCGlobal.h
    QCurlRuntime.h
    QCCookie.h
    QCCookieAsyncResult.h
    QCNetworkAccessManager.h
    QCNetworkRequestConfig.h
    QCNetworkRequest.h
    QCNetworkSslConfig.h
    QCNetworkProxyConfig.h
    QCNetworkTimeoutConfig.h
    QCNetworkHttpMethod.h
    QCNetworkHttpVersion.h
    QCNetworkLaneKey.h
    QCNetworkLaneCancelResult.h
    QCNetworkSchedulerPolicy.h
    QCNetworkError.h
    QCNetworkReply.h
    QCNetworkRetryPolicy.h
    QCNetworkRequestPriority.h
    QCNetworkRequestScheduler.h
    QCNetworkCachePolicy.h
    QCNetworkCache.h
    QCNetworkCacheRequestKey.h
    QCNetworkMemoryCache.h
    QCNetworkDiskCache.h
    QCMultipartFormData.h
    QCNetworkBody.h
    QCNetworkMultipartBody.h
    QCNetworkTransferJob.h
    QCNetworkDownloadToDeviceJob.h
    QCNetworkResumableDownloadJob.h
    QCNetworkLogger.h
    QCNetworkDefaultLogger.h
    QCNetworkCancelToken.h
    QCNetworkConnectionPoolConfig.h
    QCNetworkConnectionPoolManager.h
    QCNetworkMiddleware.h
)

# 非默认组件头的聚合清单，仅供现有 manifest 生成逻辑使用。
# 组件所有权以三个独立的 QCURL_INSTALL_HEADERS_* 清单为准。
set(QCURL_INSTALL_HEADERS_EXTRAS
    QCBlockingCookieStore.h
    QCBlockingNetworkClient.h
    QCBlockingNetworkResult.h
    QCNetworkDiagnostics.h
    QCNetworkMiddlewareExtras.h
    QCNetworkMockHandler.h
    QCNetworkTestSupport.h
)

set(QCURL_INSTALL_HEADERS_BLOCKING_EXTRAS
    QCBlockingCookieStore.h
    QCBlockingNetworkClient.h
    QCBlockingNetworkResult.h
)

set(QCURL_INSTALL_HEADERS_TEST_SUPPORT
    QCNetworkMockHandler.h
    QCNetworkTestSupport.h
)

set(QCURL_INSTALL_HEADERS_OTHER_EXTRAS
    QCNetworkDiagnostics.h
    QCNetworkMiddlewareExtras.h
)

if(QCURL_WEBSOCKET_SUPPORT)
    list(APPEND QCURL_INSTALL_HEADERS_EXTRAS
        QCWebSocketCommandResult.h
        QCWebSocket.h
        QCWebSocketReconnectPolicy.h
        QCWebSocketPool.h
        QCWebSocketPoolResults.h
    )
    list(APPEND QCURL_INSTALL_HEADERS_OTHER_EXTRAS
        QCWebSocketCommandResult.h
        QCWebSocket.h
        QCWebSocketReconnectPolicy.h
        QCWebSocketPool.h
        QCWebSocketPoolResults.h
    )
endif()

set(QCURL_INSTALL_HEADERS "${QCURL_INSTALL_HEADERS}" PARENT_SCOPE)
set(QCURL_INSTALL_HEADERS_EXTRAS "${QCURL_INSTALL_HEADERS_EXTRAS}" PARENT_SCOPE)
set(QCURL_INSTALL_HEADERS_BLOCKING_EXTRAS "${QCURL_INSTALL_HEADERS_BLOCKING_EXTRAS}" PARENT_SCOPE)
set(QCURL_INSTALL_HEADERS_TEST_SUPPORT "${QCURL_INSTALL_HEADERS_TEST_SUPPORT}" PARENT_SCOPE)
set(QCURL_INSTALL_HEADERS_OTHER_EXTRAS "${QCURL_INSTALL_HEADERS_OTHER_EXTRAS}" PARENT_SCOPE)
set(QCURL_GENERATED_CONFIG_HEADER "${CMAKE_BINARY_DIR}/src/QCurlConfig.h" PARENT_SCOPE)

# 创建 QCurl 库。默认发布 shared；static 需要显式 opt-in，并有独立 gate 证明。
set(_qcurl_library_type STATIC)
if(QCURL_BUILD_SHARED_LIBS)
    set(_qcurl_library_type SHARED)
endif()
add_library(QCurl ${_qcurl_library_type}
    ${QCURL_ALL_SOURCES}
    ${QCURL_BLOCKING_EXTRAS_ALL_SOURCES}
    ${QCURL_CORE_HEADERS}
    ${QCURL_INSTALL_HEADERS_BLOCKING_EXTRAS}
)

# 添加库别名（用于 CMake 导出）
add_library(QCurl::QCurl ALIAS QCurl)

add_library(QCurlBlockingExtras INTERFACE)
add_library(QCurl::BlockingExtras ALIAS QCurlBlockingExtras)

add_library(QCurlTestSupport STATIC
    ${QCURL_TEST_SUPPORT_ALL_SOURCES}
    ${QCURL_INSTALL_HEADERS_TEST_SUPPORT}
)
add_library(QCurl::TestSupport ALIAS QCurlTestSupport)

add_library(QCurlOtherExtras ${_qcurl_library_type}
    ${QCURL_OTHER_EXTRAS_ALL_SOURCES}
    ${QCURL_OTHER_EXTRAS_HEADERS}
)
add_library(QCurl::OtherExtras ALIAS QCurlOtherExtras)

if(QCURL_BUILD_SHARED_LIBS)
    foreach(_qcurl_shared_target IN ITEMS QCurl QCurlOtherExtras)
        set_target_properties(${_qcurl_shared_target} PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN YES
        )
    endforeach()
endif()

qcurl_enable_qt_no_keywords(QCurl)
qcurl_enable_qt_no_keywords(QCurlTestSupport)
qcurl_enable_qt_no_keywords(QCurlOtherExtras)

# 设置库属性
set_target_properties(QCurl PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 2
    AUTOMOC ON  # 自动处理 Qt MOC
    EXPORT_NAME QCurl
    POSITION_INDEPENDENT_CODE ON
)
set_target_properties(QCurlOtherExtras PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 2
    AUTOMOC ON
    POSITION_INDEPENDENT_CODE ON
)
set_target_properties(QCurlBlockingExtras PROPERTIES
    EXPORT_NAME BlockingExtras
)
set_target_properties(QCurlTestSupport PROPERTIES
    AUTOMOC ON
    EXPORT_NAME TestSupport
    POSITION_INDEPENDENT_CODE ON
)
set_target_properties(QCurlOtherExtras PROPERTIES EXPORT_NAME OtherExtras)

set(_qcurl_component_targets
    QCurl
    QCurlBlockingExtras
    QCurlTestSupport
    QCurlOtherExtras
)

# 链接依赖
target_link_libraries(QCurl
    PUBLIC
        Qt6::Core
)

target_link_libraries(QCurlBlockingExtras
    INTERFACE
        QCurl
)

target_link_libraries(QCurlTestSupport
    INTERFACE
        QCurl
)

if(NOT QCURL_BUILD_SHARED_LIBS)
    target_link_libraries(QCurlOtherExtras
        PUBLIC
            QCurl
            Qt6::Network
    )
else()
    target_link_libraries(QCurlOtherExtras
        PUBLIC
            QCurl
            Qt6::Network
    )
endif()

if(QCURL_BUILD_LIBCURL_CONSISTENCY)
    target_include_directories(QCurl PRIVATE "${CMAKE_SOURCE_DIR}/curl/include")
    target_link_libraries(QCurl PRIVATE "$<BUILD_INTERFACE:$<TARGET_FILE:libcurl_shared>>")
    add_dependencies(QCurl libcurl_shared)
    target_include_directories(QCurlOtherExtras PRIVATE "${CMAKE_SOURCE_DIR}/curl/include")
    target_link_libraries(QCurlOtherExtras PRIVATE "$<BUILD_INTERFACE:$<TARGET_FILE:libcurl_shared>>")
    add_dependencies(QCurlOtherExtras libcurl_shared)
else()
    if(NOT QCURL_BUILD_SHARED_LIBS)
        target_link_libraries(QCurl PUBLIC CURL::libcurl)
        target_link_libraries(QCurlOtherExtras PUBLIC CURL::libcurl)
    else()
        target_link_libraries(QCurl PRIVATE CURL::libcurl ZLIB::ZLIB)
        target_link_libraries(QCurlOtherExtras PRIVATE CURL::libcurl)
    endif()
endif()

# 包含目录
target_include_directories(QCurl
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/src>  # 用于 QCurlConfig.h
        $<INSTALL_INTERFACE:include/qcurl>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/private
)
target_include_directories(QCurlBlockingExtras INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/src>
    $<INSTALL_INTERFACE:include/qcurl>
)
target_include_directories(QCurlTestSupport
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/src>
        $<INSTALL_INTERFACE:include/qcurl>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/private
)
target_include_directories(QCurlOtherExtras
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/src>
        $<INSTALL_INTERFACE:include/qcurl>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/private
)

# 编译定义
target_compile_definitions(QCurl
    PRIVATE
        $<$<BOOL:${QCURL_BUILD_SHARED_LIBS}>:QCURL_BUILDING_LIBRARY>
        setWithTestHook=setWithCheckedOption
        curlEasySetoptWithTestHook=curlEasySetoptWithCheckedOption
        setProxySslVerifyHostWithTestHook=setProxySslVerifyHostWithCheckedOption
        setProxySslVerifyPeerWithTestHook=setProxySslVerifyPeerWithCheckedOption
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
        QT_NO_URL_CAST_FROM_STRING
)
target_compile_definitions(QCurlOtherExtras
    PRIVATE
        $<$<BOOL:${QCURL_BUILD_SHARED_LIBS}>:QCURL_BUILDING_OTHER_EXTRAS_LIBRARY>
        setWithTestHook=setWithCheckedOption
        curlEasySetoptWithTestHook=curlEasySetoptWithCheckedOption
        setProxySslVerifyHostWithTestHook=setProxySslVerifyHostWithCheckedOption
        setProxySslVerifyPeerWithTestHook=setProxySslVerifyPeerWithCheckedOption
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
        QT_NO_URL_CAST_FROM_STRING
)
target_compile_definitions(QCurlTestSupport
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
        QT_NO_URL_CAST_FROM_STRING
)

# 特性宏定义
if(QCURL_WEBSOCKET_SUPPORT)
    target_compile_definitions(QCurl PUBLIC QCURL_WEBSOCKET_SUPPORT)
    target_compile_definitions(QCurlOtherExtras PUBLIC QCURL_WEBSOCKET_SUPPORT)
endif()

if(QCURL_HTTP2_SUPPORT)
    target_compile_definitions(QCurl PUBLIC QCURL_HTTP2_SUPPORT)
    target_compile_definitions(QCurlOtherExtras PUBLIC QCURL_HTTP2_SUPPORT)
endif()

# Shared release libraries keep a minimal dynamic export surface. White-box Qt tests
# instead link non-installed static companions so private implementation details never
# need QCURL_EXPORT solely for test linkage.
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

# 安装头文件
install(FILES ${QCURL_INSTALL_HEADERS}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qcurl
        COMPONENT Development)

install(FILES ${QCURL_INSTALL_HEADERS_BLOCKING_EXTRAS}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qcurl
        COMPONENT BlockingExtrasDevelopment)

install(FILES ${QCURL_INSTALL_HEADERS_TEST_SUPPORT}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qcurl
        COMPONENT TestSupportDevelopment)

install(FILES ${QCURL_INSTALL_HEADERS_OTHER_EXTRAS}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qcurl
        COMPONENT OtherExtrasDevelopment)

# 安装配置头文件
install(FILES ${CMAKE_BINARY_DIR}/src/QCurlConfig.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qcurl
        COMPONENT Development)
