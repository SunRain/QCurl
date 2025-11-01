# QCurl 库源文件配置

# 当前保留的旧文件（v2.0 重构后剩余）
set(QCURL_LEGACY_SOURCES
    CurlGlobalConstructor.cpp
    QCNetworkAccessManager.cpp
    QCNetworkRequest.cpp
    QCUtility.cpp
)

set(QCURL_LEGACY_HEADERS
    CurlGlobalConstructor.h
    QCNetworkAccessManager.h
    QCNetworkAccessManager_p.h
    QCNetworkRequest.h
    QCUtility.h
    qbytedata_p.h
)

# 新的现代化实现（正在添加中）
set(QCURL_MODERN_SOURCES
    QCCurlHandleManager.cpp
    QCNetworkSslConfig.cpp
    QCNetworkProxyConfig.cpp
    QCNetworkTimeoutConfig.cpp
    QCNetworkHttpVersion.cpp
    QCNetworkError.cpp
    QCNetworkReply.cpp
    QCCurlMultiManager.cpp
    QCNetworkRetryPolicy.cpp
    QCNetworkRequestScheduler.cpp   # v2.6.0: 请求优先级调度
    QCRequest.cpp                   # v2.9.0: 流式 Request API
    QCRequestBuilder.cpp            # v2.9.0: 传统 Builder API
    QCMultipartFormData.cpp         # v2.11.0: Multipart 表单数据
    QCNetworkCache.cpp              # v2.12.0: 缓存基类
    QCNetworkMemoryCache.cpp        # v2.12.0: 内存缓存
    QCNetworkDiskCache.cpp          # v2.12.0: 磁盘缓存
    QCNetworkConnectionPoolManager.cpp  # v2.14.0: 连接池管理器
    QCNetworkLogger.cpp             # v2.15.0: 日志系统
    QCNetworkMiddleware.cpp         # v2.15.0: 中间件
    QCNetworkCancelToken.cpp        # v2.15.0: 取消令牌
    QCNetworkRequestBuilder.cpp     # v2.16.0: 流式构建器
    QCNetworkMockHandler.cpp        # v2.16.0: Mock 工具
    QCNetworkDiagnostics.cpp        # v2.19.0: 网络诊断工具
)

set(QCURL_MODERN_HEADERS
    QCGlobal.h
    QCCurlHandleManager.h
    QCNetworkSslConfig.h
    QCNetworkProxyConfig.h
    QCNetworkTimeoutConfig.h
    QCNetworkHttpVersion.h
    QCNetworkError.h
    QCNetworkReply.h
    QCNetworkReply_p.h
    QCCurlMultiManager.h
    QCNetworkRetryPolicy.h
    QCNetworkRequestPriority.h      # v2.6.0: 优先级枚举
    QCNetworkRequestScheduler.h     # v2.6.0: 请求调度器
    QCRequest.h                     # v2.9.0: 流式 Request API
    QCRequestBuilder.h              # v2.9.0: 传统 Builder API
    QCMultipartFormData.h           # v2.11.0: Multipart 表单数据
    QCNetworkCachePolicy.h          # v2.12.0: 缓存策略枚举
    QCNetworkCache.h                # v2.12.0: 缓存基类
    QCNetworkMemoryCache.h          # v2.12.0: 内存缓存
    QCNetworkDiskCache.h            # v2.12.0: 磁盘缓存
    QCNetworkConnectionPoolConfig.h # v2.14.0: 连接池配置
    QCNetworkConnectionPoolManager.h # v2.14.0: 连接池管理器
    QCNetworkLogger.h               # v2.15.0: 日志系统
    QCNetworkMiddleware.h           # v2.15.0: 中间件
    QCNetworkCancelToken.h          # v2.15.0: 取消令牌
    QCNetworkRequestBuilder.h       # v2.16.0: 流式构建器
    QCNetworkMockHandler.h          # v2.16.0: Mock 工具
    QCNetworkDiagnostics.h          # v2.19.0: 网络诊断工具
)

# ============================================================================
# WebSocket 支持（v2.3.0）+ 重连机制（v2.4.0）
# ============================================================================
if(QCURL_WEBSOCKET_SUPPORT)
    list(APPEND QCURL_MODERN_SOURCES
        QCWebSocket.cpp
        QCWebSocketReconnectPolicy.cpp
        QCWebSocketPool.cpp                 # v2.5.0: WebSocket 连接池
        QCWebSocketCompressionConfig.cpp    # v2.18.0: WebSocket 压缩配置
    )
    list(APPEND QCURL_MODERN_HEADERS
        QCWebSocket.h
        QCWebSocketReconnectPolicy.h
        QCWebSocketPool.h                   # v2.5.0: WebSocket 连接池
        QCWebSocketCompressionConfig.h      # v2.18.0: WebSocket 压缩配置
    )
    list(APPEND QCURL_MODERN_HEADERS
        QCWebSocket_p.h
    )
    message(STATUS "✅ WebSocket 支持已启用（libcurl ${CURL_VERSION_STRING} + v2.4.0 重连 + v2.5.0 连接池 + v2.18.0 压缩）")
else()
    message(STATUS "⏭️ WebSocket 支持未启用（需要 libcurl 7.86.0+）")
endif()

# 合并所有源文件和头文件
set(QCURL_ALL_SOURCES ${QCURL_LEGACY_SOURCES} ${QCURL_MODERN_SOURCES})
set(QCURL_ALL_HEADERS ${QCURL_LEGACY_HEADERS} ${QCURL_MODERN_HEADERS})

# 创建 QCurl 共享库
add_library(QCurl SHARED ${QCURL_ALL_SOURCES} ${QCURL_ALL_HEADERS})

# 添加库别名（用于 CMake 导出）
add_library(QCurl::QCurl ALIAS QCurl)

# 设置库属性
set_target_properties(QCurl PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 2
    AUTOMOC ON  # 自动处理 Qt MOC
    EXPORT_NAME QCurl
)

# 链接依赖
target_link_libraries(QCurl
    PUBLIC
        Qt6::Core
        Qt6::Network
    PRIVATE
        CURL::libcurl
        ${ZLIB_LIBRARIES}  # v2.18.0: WebSocket 压缩
)

# 包含目录
target_include_directories(QCurl
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/src>  # 用于 QCurlConfig.h
        $<INSTALL_INTERFACE:include/qcurl>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/private  # 用于 SingletonPointer.h
        ${CURL_INCLUDE_DIRS}
)

# 编译定义
target_compile_definitions(QCurl
    PRIVATE
        # 暂时禁用严格的 Qt 字符串转换检查，以便旧代码能够编译
        # TODO: 在后续阶段逐步修复并重新启用这些定义
        # QT_NO_CAST_FROM_ASCII
        # QT_NO_CAST_TO_ASCII
        # QT_NO_URL_CAST_FROM_STRING
)

# 特性宏定义
if(QCURL_WEBSOCKET_SUPPORT)
    target_compile_definitions(QCurl PUBLIC QCURL_WEBSOCKET_SUPPORT)
endif()

if(QCURL_HTTP2_SUPPORT)
    target_compile_definitions(QCurl PUBLIC QCURL_HTTP2_SUPPORT)
endif()

# 安装头文件
install(FILES ${QCURL_ALL_HEADERS}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qcurl
        COMPONENT Development)

# 安装配置头文件
install(FILES ${CMAKE_BINARY_DIR}/src/QCurlConfig.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qcurl
        COMPONENT Development)
