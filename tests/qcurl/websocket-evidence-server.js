#!/usr/bin/env node
/* eslint-disable no-console */
'use strict';

/**
 * WebSocket 协议层证据服务器（raw frame / 零外部依赖）
 *
 * 目标：
 * - 提供“可证伪 + 可复核”的 WebSocket 协议层证据（frame-level）。
 * - 显式发送 fragmentation（FIN=0 + continuation frames）与 server-initiated close（code/reason）。
 * - 输出 READY marker（单行 JSON）与工件路径，便于 QtTest 侧解析与归档。
 *
 * 约束：
 * - 不依赖 node_modules；仅使用 Node 内置模块（net/tls/crypto/fs/path/os）。
 * - 工件仅记录摘要（len/sha256/opcode/fin），不记录 payload 原文，避免泄露敏感信息。
 *
 * 用法：
 *   node tests/qcurl/websocket-evidence-server.js --port 0
 *   node tests/qcurl/websocket-evidence-server.js --tls --cert <path> --key <path> --port 0
 *
 * READY marker（单行）：
 *   QCURL_WEBSOCKET_TEST_SERVER_READY {"port":12345,"kind":"evidence","artifactsPath":"...","nodeVersion":"v20.0.0"}
 *
 * 场景（通过 path/query 选择）：
 * - /fragment?type=binary&len=4096&parts=3&seed=1&case=xxx
 *   服务端握手后主动发送“分片消息”（FIN=0 + continuation），证明 fragmentation 真实发生。
 * - /close?code=1001&reason=bye&case=xxx
 *   服务端握手后主动发送 close(code/reason) 并断开，证明 server-initiated close 可观测。
 */

const crypto = require('crypto');
const fs = require('fs');
const net = require('net');
const os = require('os');
const path = require('path');
const tls = require('tls');

const WS_GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';

function parseArgs(argv) {
  const args = argv.slice(2);
  const result = {
    host: '127.0.0.1',
    port: 0,
    tls: false,
    certPath: '',
    keyPath: '',
  };
  for (let i = 0; i < args.length; i += 1) {
    const a = args[i];
    if (a === '--tls') {
      result.tls = true;
      continue;
    }
    if (a === '--host' && i + 1 < args.length) {
      result.host = String(args[i + 1]);
      i += 1;
      continue;
    }
    if (a.startsWith('--host=')) {
      result.host = String(a.slice('--host='.length));
      continue;
    }
    if (a === '--port' && i + 1 < args.length) {
      result.port = Number.parseInt(String(args[i + 1]), 10);
      i += 1;
      continue;
    }
    if (a.startsWith('--port=')) {
      result.port = Number.parseInt(String(a.slice('--port='.length)), 10);
      continue;
    }
    if (a === '--cert' && i + 1 < args.length) {
      result.certPath = String(args[i + 1]);
      i += 1;
      continue;
    }
    if (a.startsWith('--cert=')) {
      result.certPath = String(a.slice('--cert='.length));
      continue;
    }
    if (a === '--key' && i + 1 < args.length) {
      result.keyPath = String(args[i + 1]);
      i += 1;
      continue;
    }
    if (a.startsWith('--key=')) {
      result.keyPath = String(a.slice('--key='.length));
      continue;
    }
  }

  if (!Number.isInteger(result.port) || result.port < 0 || result.port > 65535) {
    throw new Error(`invalid port: ${String(result.port)}`);
  }
  return result;
}

function sha256Hex(buf) {
  return crypto.createHash('sha256').update(buf).digest('hex');
}

function splitParts(totalLen, parts) {
  const n = Math.max(1, Math.min(128, Number.parseInt(String(parts), 10) || 1));
  const base = Math.floor(totalLen / n);
  const rem = totalLen % n;
  const sizes = [];
  for (let i = 0; i < n; i += 1) {
    sizes.push(base + (i < rem ? 1 : 0));
  }
  return sizes.filter((x) => x > 0);
}

function buildWsFrame(opcode, payload, fin) {
  const payloadLen = payload.length;
  let header = null;
  if (payloadLen <= 125) {
    header = Buffer.alloc(2);
    header[1] = payloadLen;
  } else if (payloadLen <= 0xffff) {
    header = Buffer.alloc(4);
    header[1] = 126;
    header.writeUInt16BE(payloadLen, 2);
  } else {
    header = Buffer.alloc(10);
    header[1] = 127;
    // 仅支持 <= 2^32-1 的长度（测试足够）
    header.writeUInt32BE(0, 2);
    header.writeUInt32BE(payloadLen >>> 0, 6);
  }
  header[0] = (fin ? 0x80 : 0x00) | (opcode & 0x0f);
  return Buffer.concat([header, payload]);
}

function safeJsonlAppend(stream, obj) {
  try {
    stream.write(`${JSON.stringify(obj)}\n`);
  } catch (err) {
    // 避免因日志失败影响测试主流程
  }
}

function parseHttpRequestHeader(buf) {
  const sep = buf.indexOf('\r\n\r\n');
  if (sep < 0) return null;
  const head = buf.slice(0, sep).toString('utf8');
  const rest = buf.slice(sep + 4);
  const lines = head.split('\r\n').filter((x) => x.length > 0);
  if (lines.length <= 0) return null;
  const requestLine = lines[0];
  const m = /^([A-Z]+)\s+(\S+)\s+HTTP\/1\.[01]$/.exec(requestLine);
  if (!m) return null;
  const method = m[1];
  const target = m[2];
  const headers = {};
  for (const line of lines.slice(1)) {
    const idx = line.indexOf(':');
    if (idx <= 0) continue;
    const k = line.slice(0, idx).trim().toLowerCase();
    const v = line.slice(idx + 1).trim();
    if (!k) continue;
    headers[k] = v;
  }
  return { method, target, headers, rest };
}

function parseTarget(target) {
  const qIdx = target.indexOf('?');
  const pathname = qIdx >= 0 ? target.slice(0, qIdx) : target;
  const query = qIdx >= 0 ? target.slice(qIdx + 1) : '';
  const params = {};
  if (query) {
    for (const part of query.split('&')) {
      if (!part) continue;
      const eq = part.indexOf('=');
      if (eq < 0) {
        params[decodeURIComponent(part)] = '';
        continue;
      }
      const k = decodeURIComponent(part.slice(0, eq));
      const v = decodeURIComponent(part.slice(eq + 1));
      params[k] = v;
    }
  }
  return { pathname, params };
}

function wsAcceptKey(secKey) {
  return crypto
    .createHash('sha1')
    .update(String(secKey).trim() + WS_GUID, 'utf8')
    .digest('base64');
}

function writeHttpError(socket, code, msg) {
  const body = Buffer.from(String(msg || 'bad_request'), 'utf8');
  const res = [
    `HTTP/1.1 ${code} ${code === 400 ? 'Bad Request' : 'Error'}`,
    'Connection: close',
    'Content-Type: text/plain; charset=utf-8',
    `Content-Length: ${body.length}`,
    '',
    '',
  ].join('\r\n');
  socket.write(res);
  socket.write(body);
  socket.end();
}

function ensureArtifactsPath() {
  const root = String(
    process.env.QCURL_WEBSOCKET_EVIDENCE_DIR
      || process.env.QCURL_TEST_ARTIFACT_DIR
      || path.join(os.tmpdir(), 'qcurl-test-artifacts')
  );
  const dir = path.join(root, 'websocket-evidence');
  fs.mkdirSync(dir, { recursive: true });
  const p = path.join(dir, `ws_evidence_${process.pid}_${Date.now()}.jsonl`);
  return p;
}

function main() {
  const startedAt = Date.now();
  let cfg = null;
  try {
    cfg = parseArgs(process.argv);
  } catch (err) {
    const msg = err && err.message ? err.message : String(err);
    console.error(`QCURL_WEBSOCKET_TEST_SERVER_ERROR invalid_args ${msg}`);
    process.exit(2);
    return;
  }

  const artifactsPath = ensureArtifactsPath();
  const artifactsStream = fs.createWriteStream(artifactsPath, { flags: 'a' });
  safeJsonlAppend(artifactsStream, {
    ts: new Date().toISOString(),
    event: 'server_start',
    kind: 'evidence',
    nodeVersion: process.version,
    pid: process.pid,
    startedAtMs: startedAt,
    artifactsPath,
    tls: !!cfg.tls,
  });

  const createServer = () => {
    if (!cfg.tls) {
      return net.createServer();
    }
    const certPath = cfg.certPath || path.join(__dirname, 'testdata', 'http2', 'localhost.crt');
    const keyPath = cfg.keyPath || path.join(__dirname, 'testdata', 'http2', 'localhost.key');
    if (!fs.existsSync(certPath) || !fs.existsSync(keyPath)) {
      throw new Error(`missing tls cert/key: cert=${certPath} key=${keyPath}`);
    }
    return tls.createServer({
      cert: fs.readFileSync(certPath),
      key: fs.readFileSync(keyPath),
    });
  };

  let server = null;
  try {
    server = createServer();
  } catch (err) {
    const msg = err && err.message ? err.message : String(err);
    console.error(`QCURL_WEBSOCKET_TEST_SERVER_ERROR init_failed ${msg}`);
    process.exit(1);
    return;
  }

  let nextConnId = 1;

  server.on('connection', (socket) => {
    const connId = nextConnId++;
    let buf = Buffer.alloc(0);
    let handshakeDone = false;
    let caseId = '';
    let targetInfo = null;

    const remote = `${socket.remoteAddress || ''}:${socket.remotePort || ''}`.replace(/^:/, '');
    safeJsonlAppend(artifactsStream, {
      ts: new Date().toISOString(),
      event: 'connection',
      connId,
      remote,
    });

    const onData = (chunk) => {
      buf = Buffer.concat([buf, chunk]);
      if (handshakeDone) {
        // 证据服务器最小化：不解析 client->server frames（避免噪声/复杂度）。
        return;
      }

      const parsed = parseHttpRequestHeader(buf);
      if (!parsed) return;
      buf = parsed.rest || Buffer.alloc(0);

      if (parsed.method !== 'GET') {
        writeHttpError(socket, 400, 'expected GET');
        return;
      }

      targetInfo = parseTarget(parsed.target);
      caseId = String((targetInfo.params || {}).case || '');
      if (!caseId) {
        caseId = `conn_${connId}`;
      }

      const secKey = parsed.headers['sec-websocket-key'];
      if (!secKey) {
        writeHttpError(socket, 400, 'missing Sec-WebSocket-Key');
        return;
      }

      const accept = wsAcceptKey(secKey);
      const res = [
        'HTTP/1.1 101 Switching Protocols',
        'Upgrade: websocket',
        'Connection: Upgrade',
        `Sec-WebSocket-Accept: ${accept}`,
        '',
        '',
      ].join('\r\n');
      socket.write(res);
      handshakeDone = true;

      safeJsonlAppend(artifactsStream, {
        ts: new Date().toISOString(),
        event: 'handshake_ok',
        connId,
        case: caseId,
        target: parsed.target,
        path: targetInfo.pathname,
      });

      // 场景选择：握手成功后立即执行一次“可证伪动作”。
      try {
        if (targetInfo.pathname === '/fragment') {
          const type = String(targetInfo.params.type || 'binary');
          const totalLen = Math.max(1, Math.min(1024 * 1024, Number.parseInt(String(targetInfo.params.len || '4096'), 10) || 4096));
          const parts = Math.max(2, Math.min(32, Number.parseInt(String(targetInfo.params.parts || '3'), 10) || 3));
          const seed = Math.max(0, Math.min(255, Number.parseInt(String(targetInfo.params.seed || '0'), 10) || 0));
          const sizes = splitParts(totalLen, parts);
          const opcode = (type === 'text') ? 0x1 : 0x2;

          let payload = null;
          if (type === 'text') {
            const chars = [];
            for (let i = 0; i < totalLen; i += 1) {
              chars.push(String.fromCharCode(97 + (i % 26)));
            }
            payload = Buffer.from(chars.join(''), 'utf8');
          } else {
            payload = Buffer.alloc(totalLen);
            for (let i = 0; i < totalLen; i += 1) {
              payload[i] = (seed + i) & 0xff;
            }
          }

          safeJsonlAppend(artifactsStream, {
            ts: new Date().toISOString(),
            event: 'scenario_fragment_begin',
            connId,
            case: caseId,
            type,
            totalLen: payload.length,
            totalSha256: sha256Hex(payload),
            parts: sizes.length,
          });

          let offset = 0;
          for (let i = 0; i < sizes.length; i += 1) {
            const size = sizes[i];
            const chunkPayload = payload.slice(offset, offset + size);
            offset += size;
            const fin = (i === sizes.length - 1);
            const op = (i === 0) ? opcode : 0x0; // continuation
            const frame = buildWsFrame(op, chunkPayload, fin);
            socket.write(frame);
            safeJsonlAppend(artifactsStream, {
              ts: new Date().toISOString(),
              event: 'ws_frame',
              connId,
              case: caseId,
              direction: 'send',
              fin: fin ? 1 : 0,
              opcode: op,
              payload_len: chunkPayload.length,
              payload_sha256: sha256Hex(chunkPayload),
            });
          }

          safeJsonlAppend(artifactsStream, {
            ts: new Date().toISOString(),
            event: 'scenario_fragment_end',
            connId,
            case: caseId,
          });
          return;
        }

        if (targetInfo.pathname === '/close') {
          const code = Math.max(1000, Math.min(4999, Number.parseInt(String(targetInfo.params.code || '1001'), 10) || 1001));
          const reason = String(targetInfo.params.reason || 'bye');
          const reasonBuf = Buffer.from(reason, 'utf8');
          const payload = Buffer.alloc(2 + reasonBuf.length);
          payload.writeUInt16BE(code, 0);
          reasonBuf.copy(payload, 2);
          const frame = buildWsFrame(0x8, payload, true);
          socket.write(frame);
          safeJsonlAppend(artifactsStream, {
            ts: new Date().toISOString(),
            event: 'ws_frame',
            connId,
            case: caseId,
            direction: 'send',
            fin: 1,
            opcode: 0x8,
            payload_len: payload.length,
            payload_sha256: sha256Hex(payload),
            close_code: code,
            close_reason: reason,
          });
          safeJsonlAppend(artifactsStream, {
            ts: new Date().toISOString(),
            event: 'scenario_close_sent',
            connId,
            case: caseId,
          });
          setTimeout(() => {
            try { socket.end(); } catch (e) { /* ignore */ }
            try { socket.destroy(); } catch (e) { /* ignore */ }
          }, 50);
          return;
        }

        // 未知 path：明确拒绝（close 1008）
        const code = 1008;
        const reason = `unknown_path:${targetInfo.pathname || ''}`;
        const reasonBuf = Buffer.from(reason, 'utf8');
        const payload = Buffer.alloc(2 + reasonBuf.length);
        payload.writeUInt16BE(code, 0);
        reasonBuf.copy(payload, 2);
        socket.write(buildWsFrame(0x8, payload, true));
        safeJsonlAppend(artifactsStream, {
          ts: new Date().toISOString(),
          event: 'ws_frame',
          connId,
          case: caseId,
          direction: 'send',
          fin: 1,
          opcode: 0x8,
          payload_len: payload.length,
          payload_sha256: sha256Hex(payload),
          close_code: code,
          close_reason: reason,
        });
        setTimeout(() => {
          try { socket.end(); } catch (e) { /* ignore */ }
          try { socket.destroy(); } catch (e) { /* ignore */ }
        }, 50);
      } catch (err) {
        const msg = err && err.message ? err.message : String(err);
        safeJsonlAppend(artifactsStream, {
          ts: new Date().toISOString(),
          event: 'scenario_error',
          connId,
          case: caseId,
          error: msg,
        });
        try { socket.destroy(); } catch (e) { /* ignore */ }
      }
    };

    socket.on('data', onData);
    socket.on('error', (err) => {
      safeJsonlAppend(artifactsStream, {
        ts: new Date().toISOString(),
        event: 'socket_error',
        connId,
        case: caseId,
        error: (err && err.message) ? err.message : String(err),
      });
    });
    socket.on('close', () => {
      safeJsonlAppend(artifactsStream, {
        ts: new Date().toISOString(),
        event: 'connection_closed',
        connId,
        case: caseId,
      });
    });
  });

  server.on('error', (err) => {
    const msg = err && err.message ? err.message : String(err);
    console.error(`QCURL_WEBSOCKET_TEST_SERVER_ERROR listen_failed ${msg}`);
    process.exit(1);
  });

  server.listen({ host: cfg.host, port: cfg.port }, () => {
    const addr = server.address();
    const actualPort = (addr && typeof addr === 'object' && addr.port) ? addr.port : cfg.port;
    const marker = {
      port: actualPort,
      kind: 'evidence',
      artifactsPath,
      nodeVersion: process.version,
    };
    console.log(`QCURL_WEBSOCKET_TEST_SERVER_READY ${JSON.stringify(marker)}`);
  });

  process.on('SIGINT', () => {
    try {
      safeJsonlAppend(artifactsStream, {
        ts: new Date().toISOString(),
        event: 'server_shutdown',
        kind: 'evidence',
        signal: 'SIGINT',
      });
    } finally {
      try { artifactsStream.end(); } catch (e) { /* ignore */ }
      try { server.close(() => process.exit(0)); } catch (e) { process.exit(0); }
    }
  });
}

main();
