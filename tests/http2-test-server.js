#!/usr/bin/env node
/* eslint-disable no-console */
'use strict';

const fs = require('fs');
const http2 = require('http2');
const https = require('https');
const path = require('path');
const { URL } = require('url');

function parseArgs(argv) {
  const result = {};
  for (let i = 2; i < argv.length; i++) {
    const arg = argv[i];
    if (!arg.startsWith('--')) continue;

    const key = arg.slice(2);
    const next = argv[i + 1];
    if (!next || next.startsWith('--')) {
      result[key] = true;
      continue;
    }

    result[key] = next;
    i++;
  }
  return result;
}

function parseIntOr(value, fallback) {
  if (value === undefined || value === null) return fallback;
  const n = Number.parseInt(String(value), 10);
  return Number.isFinite(n) ? n : fallback;
}

function pickEchoHeaders(headersObj) {
  const echoed = {};
  for (const [key, value] of Object.entries(headersObj || {})) {
    if (typeof value !== 'string') continue;
    const k = key.toLowerCase();
    if (k.startsWith('x-')) {
      echoed[k] = value;
    }
  }
  return echoed;
}

function toQueryObject(searchParams) {
  const obj = {};
  for (const [k, v] of searchParams.entries()) {
    obj[k] = v;
  }
  return obj;
}

async function listen(server, host, port) {
  return await new Promise((resolve, reject) => {
    const onError = (err) => reject(err);
    server.once('error', onError);
    server.listen(port, host, () => {
      server.removeListener('error', onError);
      const addr = server.address();
      resolve(addr && typeof addr === 'object' ? addr.port : port);
    });
  });
}

async function closeServer(server) {
  if (!server) return;
  return await new Promise((resolve) => {
    server.close(() => resolve());
  });
}

async function main() {
  const args = parseArgs(process.argv);

  const host = String(args.host || process.env.QCURL_HTTP2_TEST_HOST || '127.0.0.1');
  const h2Port = parseIntOr(args['h2-port'] || process.env.QCURL_HTTP2_TEST_H2_PORT, 0);
  const http1Port = parseIntOr(
    args['http1-port'] || process.env.QCURL_HTTP2_TEST_HTTP1_PORT,
    0
  );

  const keyPath = String(
    args.key
      || process.env.QCURL_HTTP2_TEST_TLS_KEY
      || path.join(__dirname, 'testdata', 'http2', 'localhost.key')
  );
  const certPath = String(
    args.cert
      || process.env.QCURL_HTTP2_TEST_TLS_CERT
      || path.join(__dirname, 'testdata', 'http2', 'localhost.crt')
  );

  const key = fs.readFileSync(keyPath);
  const cert = fs.readFileSync(certPath);

  // ----------------------------
  // HTTP/2 over TLS (h2) server
  // ----------------------------

  let nextH2SessionId = 1;
  const h2SessionIds = new WeakMap();

  const h2Server = http2.createSecureServer({
    key,
    cert,
    allowHTTP1: false,
  });

  h2Server.on('session', (session) => {
    h2SessionIds.set(session, nextH2SessionId++);
  });

  h2Server.on('stream', (stream, headers) => {
    const method = headers[':method'] || 'GET';
    const authority = headers[':authority'] || 'localhost';
    const rawPath = headers[':path'] || '/';
    const url = new URL(`https://${authority}${rawPath}`);

    const delayMs = parseIntOr(url.searchParams.get('delay_ms'), 0);
    const sessionId = h2SessionIds.get(stream.session) || 0;
    const streamId = stream.id || 0;

    const base = {
      httpVersion: '2.0',
      sessionId,
      streamId,
      method,
      path: url.pathname,
      query: toQueryObject(url.searchParams),
      headers: pickEchoHeaders(headers),
    };

    const respondJson = (status, obj) => {
      stream.respond({
        ':status': status,
        'content-type': 'application/json',
      });
      stream.end(JSON.stringify(obj));
    };

    if (url.pathname !== '/reqinfo') {
      respondJson(404, {
        ok: false,
        error: 'not_found',
        ...base,
      });
      return;
    }

    const payload = {
      ok: true,
      ...base,
    };

    if (delayMs > 0) {
      setTimeout(() => respondJson(200, payload), delayMs);
    } else {
      respondJson(200, payload);
    }
  });

  // ----------------------------
  // HTTPS HTTP/1.1-only server
  // ----------------------------

  let nextHttp1ConnId = 1;
  const http1ConnIds = new WeakMap();

  const http1Server = https.createServer({ key, cert }, (req, res) => {
    const url = new URL(req.url || '/', `https://${req.headers.host || 'localhost'}`);

    const delayMs = parseIntOr(url.searchParams.get('delay_ms'), 0);
    const httpVersion = req.httpVersion || '1.1';

    let sessionId = http1ConnIds.get(req.socket);
    if (!sessionId) {
      sessionId = nextHttp1ConnId++;
      http1ConnIds.set(req.socket, sessionId);
    }

    const base = {
      httpVersion,
      sessionId,
      streamId: 0,
      method: req.method || 'GET',
      path: url.pathname,
      query: toQueryObject(url.searchParams),
      headers: pickEchoHeaders(req.headers),
    };

    const respondJson = (status, obj) => {
      res.writeHead(status, { 'content-type': 'application/json' });
      res.end(JSON.stringify(obj));
    };

    if (url.pathname !== '/reqinfo') {
      respondJson(404, { ok: false, error: 'not_found', ...base });
      return;
    }

    const payload = { ok: true, ...base };
    if (delayMs > 0) {
      setTimeout(() => respondJson(200, payload), delayMs);
    } else {
      respondJson(200, payload);
    }
  });

  http1Server.on('connection', (socket) => {
    if (!http1ConnIds.has(socket)) {
      http1ConnIds.set(socket, nextHttp1ConnId++);
    }
  });

  const actualH2Port = await listen(h2Server, host, h2Port);
  const actualHttp1Port = await listen(http1Server, host, http1Port);

  console.log(
    `QCURL_HTTP2_TEST_SERVER_READY ${JSON.stringify({
      host,
      h2Port: actualH2Port,
      http1Port: actualHttp1Port,
      keyPath,
      certPath,
    })}`
  );

  let shuttingDown = false;
  const shutdown = async (signal) => {
    if (shuttingDown) return;
    shuttingDown = true;
    console.log(`QCURL_HTTP2_TEST_SERVER_SHUTDOWN ${signal || ''}`.trim());
    await closeServer(http1Server);
    await closeServer(h2Server);
    process.exit(0);
  };

  process.on('SIGTERM', () => shutdown('SIGTERM'));
  process.on('SIGINT', () => shutdown('SIGINT'));
}

main().catch((err) => {
  console.error('QCURL_HTTP2_TEST_SERVER_FATAL', err && err.stack ? err.stack : String(err));
  process.exit(1);
});

