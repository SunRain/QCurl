# UCE manifest@v1

`manifest@v1` 是 UCE evidence bundle 的稳定机器合同。它回答三个问题：

1. 本次 gate 跑了什么；
2. 哪些 contract / artifact / capability 被真正产出；
3. 是否存在任何 fail-closed 违例。

## 1. 稳定性规则

- `schema_version=1` 的既有字段语义不得重写，只能新增 optional 字段。
- 删除字段、修改枚举含义、调整 `policy_violations` 既有 code 语义，都属于 breaking change。
- 机器消费端遇到未知 field 可忽略；遇到未知 `policy_violations` code 必须按失败处理。

## 2. 顶层字段

| 字段 | 类型 | 必填 | 含义 | fail-closed 语义 |
|------|------|------|------|------------------|
| `schema_version` | `number` | 是 | manifest 版本 | 缺失或不匹配即失败 |
| `gate_id` | `string` | 是 | runner 标识，如 `uce` / `basic-no-problem` | 缺失即失败 |
| `tier` | `string` | 是 | `pr` / `nightly` / `soak` | 缺失即失败 |
| `run_id` | `string` | 是 | evidence 运行 ID | 缺失即失败 |
| `result` | `string` | 是 | 顶层结论：`pass` / `fail` | 仅 `pass` 允许通过 |
| `generated_at_utc` | `string` | 是 | UTC ISO8601 时间戳 | 缺失即失败 |
| `repo_root` | `string` | 是 | 仓库根目录绝对路径 | 缺失即失败 |
| `build_dir` | `string` | 是 | 构建目录绝对路径 | 缺失即失败 |
| `evidence_dir` | `string` | 是 | 当前 evidence bundle 根目录绝对路径 | 缺失即失败 |
| `tar_gz` | `string` | 是 | 归档包路径 | 文件缺失即失败 |
| `environment` | `object` | 是 | 与 gate 可复跑相关的环境快照 | 缺失即失败 |
| `results` | `array<object>` | 是 | 子 gate / validator / provider 执行结果 | 为空即失败 |
| `artifacts` | `object` | 是 | 所有可归档工件清单 | 缺失或 required artifact 缺失即失败 |
| `contracts` | `object` | 是 | contract 级结果索引 | 缺失或 required contract 无记录即失败 |
| `policy_violations` | `array<string>` | 是 | 强口径违例码 | 非空即失败 |
| `capabilities` | `object` | 否 | provider capability snapshot，如 netproof | required provider 缺失时必须反映到 `policy_violations` |

## 3. `results[]` 字段

每个 `results[]` 项表示一次 gate / provider / validator 的真实执行结果。

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `id` | `string` | 是 | 结果 ID，建议 snake_case |
| `kind` | `string` | 是 | `gate` / `provider` / `validator` / `capability` |
| `result` | `string` | 是 | `pass` / `fail` / `error` / `skipped` |
| `returncode` | `number` | 否 | 原始退出码 |
| `duration_s` | `number` | 否 | 持续时间 |
| `log_file` | `string` | 否 | 关联日志路径（建议相对 `evidence_dir`） |
| `details` | `object` | 否 | 少量结构化补充信息 |

规则：

- `result="skipped"` 不允许单独视为通过，必须同时进入 `policy_violations`。
- `returncode=0` 不代表通过；以 `result` 与 `policy_violations` 为准。

## 4. `artifacts` 字段

`artifacts` 是一个以 artifact ID 为 key 的对象：

```json
{
  "manifest": {
    "path": "manifest.json",
    "kind": "metadata",
    "required": true
  }
}
```

### 4.1 Artifact 元数据字段

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `path` | `string` | 是 | 推荐相对 `evidence_dir` |
| `kind` | `string` | 是 | `metadata` / `log` / `report` / `archive` / `evidence` / `capability` |
| `required` | `boolean` | 是 | 缺失是否直接导致失败 |
| `media_type` | `string` | 否 | 如 `application/json` |
| `classification` | `string` | 否 | `text` / `binary` / `raw_bytes`（见 `evidence@v1`） |
| `byte_count` | `number` | 否 | 文件字节数 |
| `sha256` | `string` | 否 | 文件摘要 |
| `redaction` | `object` | 否 | 脱敏状态与规则 |
| `contract_refs` | `array<string>` | 否 | 关联 contract ID，如 `hes@v1` |
| `notes` | `array<string>` | 否 | 人类可读补充说明 |

规则：

- `required=true` 且 `path` 指向文件缺失，必须进入 `policy_violations`。
- `kind="evidence"` 的 artifact 必须符合 `docs/uce/schema/evidence@v1.md`。

## 5. `contracts` 字段

`contracts` 以 contract ID 为 key，value 为 contract 级摘要。

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `provider` | `string` | 是 | 产生该 contract evidence 的 provider |
| `result` | `string` | 是 | `pass` / `fail` / `error` / `not_covered` |
| `required` | `boolean` | 是 | 当前 tier 是否把该 contract 视为必需 |
| `report_artifact` | `string` | 否 | 指向 `artifacts` key |
| `evidence_artifacts` | `array<string>` | 否 | 相关 evidence artifact keys |
| `violations` | `array<string>` | 否 | 直接关联的 `policy_violations` code |
| `notes` | `array<string>` | 否 | 覆盖边界 / 降级说明 |

规则：

- `required=true` 且 `result!="pass"`，必须进入 `policy_violations`。
- `result="not_covered"` 只能出现在当前 tier 明确允许不覆盖时，且必须附 `notes`。

## 6. `policy_violations` 码表规则

### 6.1 命名规范

- 统一使用小写 snake_case。
- 推荐按来源前缀分组：
  - `gate_*`
  - `contract_*`
  - `capability_*`
  - `redaction_*`
  - `packaging_*`
  - `artifact_*`
  - `env_preflight_*`

### 6.2 兼容策略

- 既有 code 不改语义。
- 新增 code 时，先更新稳定字典，再在脚本里落地。
- 未登记 code 被消费端发现时，按失败处理。

### 6.3 与现有字典的关系

UCE 继续复用仓库内已有的 `policy_violations` 稳定字典口径，避免 `basic-no-problem`、`libcurl_consistency` 与后续 UCE provider 出现三套 code 语义。

## 7. `capabilities` 字段

`capabilities` 用于记录 provider capability snapshot，当前至少预留：

```json
{
  "netproof": {
    "schema_version": 1,
    "providers": {
      "strace": {
        "available": true,
        "capability": "available",
        "downgrade_reason": null
      }
    },
    "tiers": {
      "nightly": {
        "capability": "full",
        "required_providers": ["strace"]
      }
    }
  }
}
```

规则：

- `required_providers` 缺失时，必须同步写入 `policy_violations`。
- provider 缺失但当前 tier 允许降级时，必须记录 `downgrade_reason`。

## 8. 最小 JSON 示例

```json
{
  "schema_version": 1,
  "gate_id": "uce",
  "tier": "pr",
  "run_id": "20260326T111600Z",
  "result": "fail",
  "generated_at_utc": "2026-03-26T11:16:00Z",
  "repo_root": "/repo",
  "build_dir": "/repo/build",
  "evidence_dir": "/repo/build/evidence/uce/20260326T111600Z",
  "tar_gz": "/repo/build/evidence/uce/20260326T111600Z.tar.gz",
  "environment": {
    "platform": "Linux",
    "python": "3.12"
  },
  "results": [
    {
      "id": "netproof_capability",
      "kind": "capability",
      "result": "pass",
      "log_file": "logs/netproof_capability.log"
    }
  ],
  "artifacts": {
    "manifest": {
      "path": "manifest.json",
      "kind": "metadata",
      "required": true
    },
    "netproof_capability": {
      "path": "meta/netproof_capabilities.json",
      "kind": "capability",
      "required": false,
      "media_type": "application/json"
    }
  },
  "contracts": {
    "timeline@v1": {
      "provider": "tlc",
      "result": "not_covered",
      "required": false,
      "notes": ["pr tier 暂未接入 Qt/DCI timeline 聚合"]
    }
  },
  "policy_violations": [
    "contract_timeline_not_covered"
  ],
  "capabilities": {
    "netproof": {
      "schema_version": 1,
      "tiers": {
        "pr": {
          "capability": "degraded",
          "required_providers": [],
          "missing_required_providers": [],
          "optional_missing_providers": ["strace", "tc", "netns"]
        }
      }
    }
  }
}
```
