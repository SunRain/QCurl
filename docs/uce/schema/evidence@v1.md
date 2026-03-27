# UCE evidence@v1

`evidence@v1` 规定 UCE 中“什么可以落盘、什么必须脱敏、什么必须禁止落盘”。

核心原则：

- 默认优先保存**结构化摘要**，而不是原始大块字节。
- 原始字节只在 contract 需要“字节级证明”时允许保留。
- 任何 evidence 落盘前都要经过脱敏规则；违规不允许以“仅供排障”名义豁免。

## 1. 证据分类

| 分类 | 适用对象 | 允许内容 | 典型示例 |
|------|----------|----------|----------|
| `text` | 日志、JSON、报告、stdout/stderr | UTF-8 文本；允许脱敏后的头部与摘要 | `gate_*.json`、validator report、redaction scan |
| `binary` | 下载样本、压缩包、二进制 report | 二进制内容或其哈希 / 元数据 | sanitizer report、压缩工件、下载数据样本 |
| `raw_bytes` | 字节级 contract 证明 | 仅允许最小必要切片 | raw headers、chunk framing、encoding bytes |

## 2. 默认保留策略

### 2.1 `text`

- 允许落盘，但必须先脱敏。
- 优先保留结构化字段、摘要、哈希、长度，而不是全量 payload。
- `stdout/stderr` 一律先经 redaction 再写入报告。

### 2.2 `binary`

- 优先保留文件本体 + `sha256` + `byte_count`。
- 若二进制内容本身可能含敏感头 / token，则只保留摘要，不直接归档原始文件。
- 不能解析为稳定 contract 的大体量 payload，不应默认落盘。

### 2.3 `raw_bytes`

只允许在以下前提下落盘：

1. 对应 contract 明确要求字节级证据；
2. 结构化摘要无法表达关键差异；
3. 仅保留**最小必要切片**；
4. manifest 中能明确写出保留原因与脱敏状态。

推荐约束：

- 默认单个 raw evidence 不超过 `4 KiB`；
- 超出时必须给出截断策略（如首段 / 关键窗口 / 匹配片段）；
- 任何与认证、会话、代理认证相关的值都不得明文保留。

## 3. 明确允许保留的内容

以下内容在满足最小必要原则时允许落盘：

- HTTP request line / status line。
- header 名称、顺序、重复出现次数。
- `Content-Encoding`、`Transfer-Encoding`、`Expect: 100-continue`、chunk framing 的关键字节。
- 与 HES / TLC 直接相关的有限正文切片、长度、哈希、边界字节。
- 二进制响应样本的 `sha256`、长度、MIME type。

## 4. 必须脱敏的内容

以下内容即使在 raw bytes 模式下也不得明文落盘：

- `Authorization`
- `Proxy-Authorization`
- `Cookie`
- `Set-Cookie`
- bearer token、basic credential、session id、signed URL secret
- 明文代理口令、client secret、API key

推荐处理方式：

- header 名称保留；
- header 值替换为固定占位符，如 `[REDACTED]`；
- 若无法只替换敏感值而保留其余结构，则整条 evidence 进入 `blocked`，并写入 `policy_violations`。

## 5. 禁止落盘的内容

以下内容不得写入 evidence bundle：

- 未脱敏的认证头、cookie、session token。
- 无边界控制的完整请求 / 响应正文转储。
- 仅供一次性排障、不可稳定解释的 debug 噪声。
- 与 contract 无关的环境私密信息（本机凭据、用户目录敏感路径、私有证书内容）。

## 6. Manifest 中的 evidence 元数据

当 `manifest.artifacts.<id>.kind == "evidence"` 时，建议至少包含以下字段：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `path` | `string` | 是 | evidence 文件路径 |
| `classification` | `string` | 是 | `text` / `binary` / `raw_bytes` |
| `media_type` | `string` | 是 | MIME type |
| `required` | `boolean` | 是 | 缺失是否失败 |
| `byte_count` | `number` | 否 | 文件大小 |
| `sha256` | `string` | 否 | 摘要 |
| `contract_refs` | `array<string>` | 否 | 关联 contract，如 `hes@v1` |
| `redaction.status` | `string` | 是 | `clean` / `redacted` / `blocked` |
| `redaction.rules` | `array<string>` | 否 | 命中的脱敏规则 |
| `redaction.notes` | `array<string>` | 否 | 解释信息 |
| `retention_reason` | `string` | 否 | 为什么必须保留该 evidence |

规则：

- `raw_bytes` evidence 必须填写 `retention_reason`。
- `redaction.status="blocked"` 时，对应 evidence 不得作为通过证据使用，且必须写入 `policy_violations`。

## 7. 最小示例

### 7.1 `text`

```json
{
  "path": "reports/tlc_report.json",
  "kind": "evidence",
  "classification": "text",
  "media_type": "application/json",
  "required": true,
  "redaction": {
    "status": "clean",
    "rules": []
  },
  "contract_refs": ["timeline@v1"]
}
```

### 7.2 `binary`

```json
{
  "path": "artifacts/asan.log",
  "kind": "evidence",
  "classification": "binary",
  "media_type": "text/plain",
  "required": false,
  "byte_count": 8192,
  "sha256": "abc123..."
}
```

### 7.3 `raw_bytes`

```json
{
  "path": "artifacts/hes/resp_headers_case01.bin",
  "kind": "evidence",
  "classification": "raw_bytes",
  "media_type": "application/octet-stream",
  "required": true,
  "byte_count": 384,
  "sha256": "def456...",
  "contract_refs": ["hes@v1"],
  "retention_reason": "需要证明重复响应头与 CRLF 边界顺序。",
  "redaction": {
    "status": "redacted",
    "rules": ["auth_headers", "cookies"],
    "notes": ["保留 header names 与顺序，敏感值替换为 [REDACTED]"]
  }
}
```

## 8. 与 UCE runner 的关系

- runner 负责在 evidence 写盘前触发 redaction。
- validator 负责把 `blocked` / 缺失 / 违规写入 `policy_violations`。
- README 负责声明“当前 contract 需要哪些 raw evidence，以及哪些仍未覆盖”。
