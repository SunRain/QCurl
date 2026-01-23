#!/usr/bin/env node
/**
 * WebSocket 回显测试服务器（message-level smoke）
 *
 * ⚠️ 重要说明：
 * - 本脚本基于 `ws` 库的 message 回调；ws 会在内部完成分片重组。
 * - 因此它只能证明“消息收发链路/回显”可用，不能证明 continuation frames（帧级分片）真实发生。
 * - 如需帧级证据（fragmentation/close code&reason），请使用 `tests/websocket-evidence-server.js`。
 * 
 * 功能：
 * - Echo 服务器：回显所有收到的消息
 * - 自动处理 WebSocket 分片（由 ws 库自动处理）
 * - 支持文本和二进制消息
 * - 监听端口：默认使用动态端口（0）；可通过 `--port` 或 `QCURL_WEBSOCKET_TEST_PORT` 覆盖
 * 
 * 依赖：
 *   npm install ws
 * 
 * 运行：
 *   node tests/websocket-fragment-server.js --port 0
 * 
 * 测试：
 *   cd build && ctest -R testFragmentedMessage -V
 * 
 * @author QCurl Team
 * @since v2.4.1
 */

const WebSocket = require('ws');
const http = require('http');
const https = require('https');
const fs = require('fs');
const path = require('path');

function parsePort() {
    const args = process.argv.slice(2);
    let raw = '';
    let tls = false;
    let certPath = '';
    let keyPath = '';
    for (let i = 0; i < args.length; i += 1) {
        const a = args[i];
        if (a === '--tls') {
            tls = true;
            continue;
        }
        if (a === '--port' && i + 1 < args.length) {
            raw = args[i + 1];
            i += 1;
            continue;
        }
        if (a.startsWith('--port=')) {
            raw = a.substring('--port='.length);
            continue;
        }

        if (a === '--cert' && i + 1 < args.length) {
            certPath = args[i + 1];
            i += 1;
            continue;
        }
        if (a.startsWith('--cert=')) {
            certPath = a.substring('--cert='.length);
            continue;
        }
        if (a === '--key' && i + 1 < args.length) {
            keyPath = args[i + 1];
            i += 1;
            continue;
        }
        if (a.startsWith('--key=')) {
            keyPath = a.substring('--key='.length);
            continue;
        }
    }
    if (!raw) {
        raw = process.env.QCURL_WEBSOCKET_TEST_PORT || '';
    }

    if (!certPath) {
        certPath = process.env.QCURL_WEBSOCKET_TEST_CERT || '';
    }
    if (!keyPath) {
        keyPath = process.env.QCURL_WEBSOCKET_TEST_KEY || '';
    }

    // 默认 0：让 OS 分配可用端口，避免固定端口冲突
    if (!raw) {
        raw = '0';
    }

    const port = Number.parseInt(String(raw), 10);
    if (!Number.isInteger(port) || port < 0 || port > 65535) {
        throw new Error(`invalid port: ${raw}`);
    }

    if (tls) {
        if (!certPath) {
            certPath = path.join(__dirname, 'testdata', 'http2', 'localhost.crt');
        }
        if (!keyPath) {
            keyPath = path.join(__dirname, 'testdata', 'http2', 'localhost.key');
        }
        if (!fs.existsSync(certPath) || !fs.existsSync(keyPath)) {
            throw new Error(`missing tls cert/key: cert=${certPath} key=${keyPath}`);
        }
    }

    return { port, tls, certPath, keyPath };
}

let port = 0;
let tls = false;
let certPath = '';
let keyPath = '';
try {
    const parsed = parsePort();
    port = parsed.port;
    tls = parsed.tls;
    certPath = parsed.certPath;
    keyPath = parsed.keyPath;
} catch (err) {
    const msg = (err && err.message) ? err.message : String(err);
    console.error(`QCURL_WEBSOCKET_TEST_SERVER_ERROR invalid_args ${msg}`);
    process.exit(2);
}

const host = '127.0.0.1';

const server = tls
    ? https.createServer({
        cert: fs.readFileSync(certPath),
        key: fs.readFileSync(keyPath),
    })
    : http.createServer();

const wss = new WebSocket.Server({
    server,
    perMessageDeflate: false,  // 禁用压缩，简化测试
});

let actualPort = port;
server.on('listening', () => {
    try {
        const addr = server.address();
        if (addr && typeof addr === 'object' && addr.port) {
            actualPort = addr.port;
        }
    } catch (err) {
        // ignore
    }

    console.log('=====================================');
    console.log('  WebSocket 分片测试服务器');
    console.log('=====================================');
    const scheme = tls ? 'wss' : 'ws';
    console.log(`监听地址: ${scheme}://localhost:${actualPort}`);
    console.log('功能: Echo 服务器（回显所有消息）');
    console.log('按 Ctrl+C 停止服务器');
    console.log('=====================================\n');

    // 供测试端解析的 READY marker（必须稳定、单行）
    console.log(`QCURL_WEBSOCKET_TEST_SERVER_READY {"port":${actualPort}}`);
});

let clientCount = 0;

wss.on('error', (error) => {
    console.error('❌ WebSocket 服务器错误:', error.message);
});

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
server.on('error', (error) => {
    console.error('❌ 服务器错误:', error.message);
    if (error.code === 'EADDRINUSE') {
        console.error(`端口 ${port} 已被占用，请先关闭占用该端口的程序或改用 --port 0`);
        process.exit(1);
    }
    console.error(`QCURL_WEBSOCKET_TEST_SERVER_ERROR listen_failed ${error.message}`);
    process.exit(1);
});

server.listen({ host, port });

// 优雅关闭
process.on('SIGINT', () => {
    console.log('\n\n收到停止信号，正在关闭服务器...');
    
    wss.clients.forEach((ws) => {
        ws.close(1000, 'Server shutting down');
    });
    
    wss.close(() => server.close(() => {
        console.log('✅ 服务器已关闭');
        process.exit(0);
    }));
});

// 定期打印统计信息（每 30 秒）
setInterval(() => {
    const activeClients = wss.clients.size;
    if (activeClients > 0) {
        console.log(`\n[统计] 当前活动连接数: ${activeClients}`);
    }
}, 30000);
