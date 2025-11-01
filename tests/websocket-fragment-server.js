#!/usr/bin/env node
/**
 * WebSocket 分片测试服务器
 * 用于 QCWebSocket 分片消息完整性测试
 * 
 * 功能：
 * - Echo 服务器：回显所有收到的消息
 * - 自动处理 WebSocket 分片（由 ws 库自动处理）
 * - 支持文本和二进制消息
 * - 监听端口：8765
 * 
 * 依赖：
 *   npm install ws
 * 
 * 运行：
 *   node tests/websocket-fragment-server.js
 * 
 * 测试：
 *   cd build && ctest -R testFragmentedMessage -V
 * 
 * @author QCurl Team
 * @since v2.4.1
 */

const WebSocket = require('ws');
const port = 8765;

// 创建 WebSocket 服务器
const wss = new WebSocket.Server({ 
    port,
    perMessageDeflate: false  // 禁用压缩，简化测试
});

console.log('=====================================');
console.log('  WebSocket 分片测试服务器');
console.log('=====================================');
console.log(`监听地址: ws://localhost:${port}`);
console.log('功能: Echo 服务器（回显所有消息）');
console.log('按 Ctrl+C 停止服务器');
console.log('=====================================\n');

let clientCount = 0;

wss.on('connection', (ws, req) => {
    const clientId = ++clientCount;
    const clientIp = req.socket.remoteAddress;
    
    console.log(`[客户端 #${clientId}] ✅ 已连接（来自 ${clientIp}）`);
    
    // 接收消息
    ws.on('message', (data, isBinary) => {
        const size = data.length;
        const type = isBinary ? '二进制' : '文本';
        
        console.log(`[客户端 #${clientId}] 📨 收到${type}消息: ${size} 字节`);
        
        // Echo 回原始消息
        // ws 库会自动处理分片（大于帧大小时自动分片）
        ws.send(data, { binary: isBinary }, (err) => {
            if (err) {
                console.error(`[客户端 #${clientId}] ❌ 发送失败:`, err.message);
            } else {
                console.log(`[客户端 #${clientId}] ✅ 消息已回显: ${size} 字节`);
            }
        });
    });
    
    // Ping 消息
    ws.on('ping', (data) => {
        console.log(`[客户端 #${clientId}] 🏓 收到 Ping: ${data.length} 字节`);
        // WebSocket 库自动响应 Pong
    });
    
    // Pong 消息
    ws.on('pong', (data) => {
        console.log(`[客户端 #${clientId}] 🏓 收到 Pong: ${data.length} 字节`);
    });
    
    // 连接关闭
    ws.on('close', (code, reason) => {
        console.log(`[客户端 #${clientId}] ❌ 连接已关闭`);
        console.log(`  关闭码: ${code}`);
        console.log(`  原因: ${reason || '(无)'}`);
    });
    
    // 错误处理
    ws.on('error', (error) => {
        console.error(`[客户端 #${clientId}] ⚠️  错误:`, error.message);
    });
});

// 服务器错误处理
wss.on('error', (error) => {
    console.error('❌ 服务器错误:', error.message);
    if (error.code === 'EADDRINUSE') {
        console.error(`端口 ${port} 已被占用，请先关闭占用该端口的程序`);
        process.exit(1);
    }
});

// 优雅关闭
process.on('SIGINT', () => {
    console.log('\n\n收到停止信号，正在关闭服务器...');
    
    wss.clients.forEach((ws) => {
        ws.close(1000, 'Server shutting down');
    });
    
    wss.close(() => {
        console.log('✅ 服务器已关闭');
        process.exit(0);
    });
});

// 定期打印统计信息（每 30 秒）
setInterval(() => {
    const activeClients = wss.clients.size;
    if (activeClients > 0) {
        console.log(`\n[统计] 当前活动连接数: ${activeClients}`);
    }
}, 30000);
