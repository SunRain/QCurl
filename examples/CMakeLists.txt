# QCurl Examples

# 添加示例子目录
add_subdirectory(QCurl)

# 请求优先级调度器演示
add_subdirectory(SchedulerDemo)

#  调度器压力测试
add_subdirectory(StressTest)

# HTTP/2 示例程序（条件编译）
if(QCURL_HTTP2_SUPPORT)
    add_subdirectory(Http2Demo)  # v2.2.1: HTTP/2 演示
    message(STATUS "✅ HTTP/2 示例程序已启用（Http2Demo）")
endif()

# HTTP/3 示例程序
add_subdirectory(Http3Demo)
message(STATUS "✅ HTTP/3 示例程序已启用（Http3Demo）")

# 代理示例程序
add_subdirectory(ProxyDemo)
message(STATUS "✅ 代理示例程序已启用（ProxyDemo）")

# WebSocket 示例程序（条件编译）
if(QCURL_WEBSOCKET_SUPPORT)
    add_subdirectory(WebSocketDemo)
    add_subdirectory(WebSocketPoolDemo)  # v2.5.0: 连接池演示
    add_subdirectory(WebSocketCompressionDemo)  # v2.18.0: 压缩演示
    message(STATUS "✅ WebSocket 示例程序已启用（WebSocketDemo + WebSocketPoolDemo + WebSocketCompressionDemo）")
endif()

# 实用示例程序
add_subdirectory(FileDownloadDemo)
message(STATUS "✅ 文件下载管理器示例已启用（FileDownloadDemo）")

add_subdirectory(FileTransferDemo)
message(STATUS "✅ 新增文件传输示例已启用（FileTransferDemo）")

add_subdirectory(BatchRequestDemo)
message(STATUS "✅ 批量请求管理器示例已启用（BatchRequestDemo）")

add_subdirectory(ApiClientDemo)
message(STATUS "✅ RESTful API 客户端示例已启用（ApiClientDemo）")

# Multipart/form-data 示例程序（v2.11.0）
add_subdirectory(MultipartSimpleDemo)
message(STATUS "✅ Multipart/form-data 示例已启用（MultipartSimpleDemo）")

# 生产级增强和开发体验优化示例
add_subdirectory(LoggingDemo)
add_subdirectory(MiddlewareDemo)
add_subdirectory(CancelTokenDemo)
add_subdirectory(RequestBuilderDemo)
message(STATUS "示例已启用（Logger + Middleware + CancelToken + RequestBuilder）")

# 综合功能演示（HTTP/3 + WebSocket 压缩 + 网络诊断）
add_subdirectory(NetworkFeaturesDemo)
message(STATUS "网络功能综合演示已启用（NetworkFeaturesDemo）")

