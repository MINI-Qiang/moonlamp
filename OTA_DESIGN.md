# OTA 远程固件更新系统 — 设计与开发计划

> **项目域名**: `ota.iot.oakiot.cc`
> **目标平台**: ESP32-C3（本项目），可扩展至多项目/多产品/多硬件版本
> **基础设施**: Cloudflare Workers + KV + R2 + Analytics Engine
> **最后更新**: 2026/3/14

---

## 一、需求概述

### 1.1 核心流程

```
设备定期自动检查（WiFi 连接时，每 24h）
         ↓
   设备 → Worker: POST /api/ota/check
         ↓
  ┌─ 无更新 → 记录时间，等下次检查
  └─ 有更新 → 保存更新信息，BLE Notify 通知已连接客户端
                  ↓
         用户在 App / Web 看到"有新版本"提示
                  ↓
         用户确认升级（BLE 写入 / Web 页面按钮）
                  ↓
         下载固件（HTTPS 流式写入 OTA 分区）
                  ↓
         进度通知 → MD5 校验 → 切换分区 → 重启
                  ↓
         验证启动成功 → 确认分区（失败则自动回滚）
```

### 1.2 设计原则

| 原则 | 说明 |
|------|------|
| **多项目共享** | 单套 Worker + R2 基础设施服务多个客户/项目/产品 |
| **设备可识别** | 通过 project_id + product_id + hw_version + device_id 四级标识 |
| **用户确认** | 仅自动检查，下载升级必须用户主动触发 |
| **安全可靠** | HTTPS + MD5 校验 + 双分区回滚 |
| **数据可观测** | Analytics Engine 记录检查/下载/结果事件 |
| **免费可控** | 充分利用 Cloudflare 免费额度，零成本运维 |

---

## 二、多项目架构设计

### 2.1 三级标识体系

采用 **项目 → 产品 → 硬件版本** 三级标识，实现多客户/多项目共享同一套 OTA 基础设施：

```
project_id (项目/客户)
  └── product_id (产品线)
        └── hw_version (硬件版本)
              └── device_id (设备实例)
```

| 层级 | 字段 | 格式 | 示例 | 说明 |
|------|------|------|------|------|
| 项目 | `project_id` | 短字符串，全局唯一 | `"oakiot"` | 标识客户或项目组，拥有独立的产品线集合 |
| 产品 | `product_id` | 短字符串，项目内唯一 | `"led-light"` | 标识产品线，同项目下可有多个产品 |
| 硬件 | `hw_version` | 芯片+版本 | `"esp32c3-v1"` | 同产品可能有多个硬件修订版 |
| 设备 | `device_id` | MAC 地址 | `"AA:BB:CC:DD:EE:FF"` | 设备唯一实例标识 |
| 固件 | `fw_version` | 语义化版本 | `"1.2.3"` | 当前运行的固件版本 |

**为什么需要三级**：

| 场景 | 示例 | 如何区分 |
|------|------|----------|
| 同团队多个产品 | oakiot 下有 LED灯、传感器、网关 | project 相同，product 不同 |
| 不同客户的产品 | 客户A灯具 vs 客户B灯具 | project 不同 |
| 同产品不同硬件 | LED灯 C3 版 vs S3 版 | product 相同，hw_version 不同 |

**固件端定义（config.h）**：
```cpp
#define OTA_PROJECT_ID      "oakiot"            // 项目/客户标识
#define OTA_PRODUCT_ID      "moon-light"         // 产品标识
#define OTA_FW_VERSION      "1.0.0"             // 语义化版本号
#define OTA_HW_VERSION      "esp32c3-v1"        // 硬件版本
#define OTA_BASE_URL        "https://ota.iot.oakiot.cc"
#define OTA_CHECK_INTERVAL  86400000UL          // 检查间隔: 24小时 (ms)
#define OTA_FIRST_DELAY     60000UL             // 首次检查延迟: 60秒 (ms)
```

### 2.2 R2 存储结构（多项目）

```
R2 Bucket: oakiot-ota-firmware
├── oakiot/                           # 项目: oakiot
│   ├── led-light/                    # 产品: LED灯
│   │   ├── esp32c3-v1/               # 硬件: ESP32-C3 v1
│   │   │   ├── firmware-1.0.0.bin
│   │   │   ├── firmware-1.1.0.bin
│   │   │   └── firmware-1.2.0.bin
│   │   └── esp32c3-v2/               # 硬件: ESP32-C3 v2 (布局改版)
│   │       └── firmware-1.0.0.bin
│   ├── sensor-hub/                   # 产品: 传感器
│   │   └── esp32s3-v1/
│   │       ├── firmware-1.0.0.bin
│   │       └── firmware-1.1.0.bin
│   └── gateway/                      # 产品: 网关
│       └── esp32-v1/
│           └── firmware-1.0.0.bin
├── clientA/                          # 项目: 客户A
│   └── smart-lamp/                   # 客户A的灯具产品
│       └── esp32c3-v1/
│           └── firmware-1.0.0.bin
└── clientB/                          # 项目: 客户B
    └── led-strip/                    # 客户B的灯带产品
        └── esp32c3-v1/
            └── firmware-1.0.0.bin
```

**命名规范**: `{project_id}/{product_id}/{hw_version}/firmware-{version}.bin`

### 2.3 KV 元数据结构（多项目）

每个产品+硬件组合的最新版本信息存储在 Workers KV 中：

**Key**: `latest:{project_id}:{product_id}:{hw_version}`
**示例 Key**: `latest:oakiot:led-light:esp32c3-v1`

**Value (JSON)**:
```json
{
  "version": "1.2.0",
  "build_ts": 1741939200,
  "size": 483200,
  "md5": "a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4",
  "min_version": "1.0.0",
  "force": false,
  "changelog": "修复BLE断连问题；优化灯效流畅度",
  "filename": "firmware-1.2.0.bin",
  "updated_at": "2026-03-14T10:00:00Z"
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `version` | string | 最新固件版本号 |
| `build_ts` | number | 构建 Unix 时间戳 |
| `size` | number | 固件文件大小（字节） |
| `md5` | string | 固件 MD5 校验码（32 字符 hex） |
| `min_version` | string | 最低可升级版本（低于此版本需先升到中间版本） |
| `force` | boolean | 是否强制更新（跳过用户确认） |
| `changelog` | string | 更新日志（UTF-8，供客户端展示） |
| `filename` | string | R2 中的固件文件名 |
| `updated_at` | string | 版本发布时间 |

---

## 三、HTTP API 设计

### 3.1 检查更新

```
POST https://ota.iot.oakiot.cc/api/v1/ota/check
Content-Type: application/json

{
  "project_id": "oakiot",
  "product_id": "led-light",
  "device_id": "AA:BB:CC:DD:EE:FF",
  "fw_version": "1.0.0",
  "hw_version": "esp32c3-v1"
}
```

**响应 — 无更新**:
```json
{
  "update": false
}
```

**响应 — 有更新**:
```json
{
  "update": true,
  "version": "1.2.0",
  "size": 483200,
  "md5": "a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4",
  "changelog": "修复BLE断连问题；优化灯效流畅度",
  "force": false,
  "url": "https://ota.iot.oakiot.cc/api/v1/ota/firmware/oakiot/led-light/esp32c3-v1/firmware-1.2.0.bin"
}
```

> **设计决策**: Worker 返回完整下载 URL（由 Worker 代理 R2），而非 302 重定向。
> 原因：ESP32 HTTPClient 对重定向 + 二次 TLS 握手支持不稳定，单次连接更可靠。

### 3.2 固件下载

```
GET https://ota.iot.oakiot.cc/api/v1/ota/firmware/{project_id}/{product_id}/{hw_version}/{filename}
```

Worker 从 R2 读取二进制流并直接返回：
```
HTTP/1.1 200 OK
Content-Type: application/octet-stream
Content-Length: 483200
X-Firmware-MD5: a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4
```

### 3.3 上报更新结果（可选）

```
POST https://ota.iot.oakiot.cc/api/v1/ota/report
Content-Type: application/json

{
  "project_id": "oakiot",
  "product_id": "led-light",
  "device_id": "AA:BB:CC:DD:EE:FF",
  "event": "update_success",
  "from_version": "1.0.0",
  "to_version": "1.2.0"
}
```

**event 枚举**: `update_success` | `update_failed` | `rollback`

---

## 四、Cloudflare 基础设施详细设计

### 4.1 Workers（计算层）

**绑定域名**: `ota.iot.oakiot.cc`

**路由表**:

| 方法 | 路径 | 功能 |
|------|------|------|
| POST | `/api/v1/ota/check` | 版本检查（读 KV，比较版本） |
| GET | `/api/v1/ota/firmware/:project/:product/:hw/:file` | 固件下载（代理 R2） |
| POST | `/api/v1/ota/report` | 上报事件（写 Analytics Engine） |

**Worker 逻辑伪代码（check）**:
```js
async function handleCheck(request, env) {
  const body = await request.json();
  const { project_id, product_id, device_id, fw_version, hw_version } = body;

  // 参数校验
  if (!project_id || !product_id || !fw_version) return error(400);

  // 读取最新版本信息
  const key = `latest:${project_id}:${product_id}:${hw_version || 'default'}`;
  const meta = await env.OTA_KV.get(key, 'json');
  if (!meta) return json({ update: false });

  // 语义化版本比较
  if (!isNewer(meta.version, fw_version)) return json({ update: false });

  // 最低版本检查
  if (meta.min_version && isOlder(fw_version, meta.min_version)) {
    return json({ update: true, error: "version_too_old", min_version: meta.min_version });
  }

  // 写入 Analytics Engine 事件
  env.OTA_ANALYTICS.writeDataPoint({
    blobs: [project_id, product_id, device_id, fw_version, meta.version, 'check_has_update'],
    doubles: [meta.size],
    indexes: [`${project_id}:${product_id}`]
  });

  return json({
    update: true,
    version: meta.version,
    size: meta.size,
    md5: meta.md5,
    changelog: meta.changelog,
    force: meta.force || false,
    url: `https://ota.iot.oakiot.cc/api/v1/ota/firmware/${project_id}/${product_id}/${hw_version}/${meta.filename}`
  });
}
```

### 4.2 Workers KV（元数据层）

**KV Namespace**: `OTA_KV`

| Key 模式 | 用途 | 示例 |
|----------|------|------|
| `latest:{project_id}:{product_id}:{hw_version}` | 某项目某产品某硬件的最新版本 | `latest:oakiot:led-light:esp32c3-v1` |
| `projects` | 已注册项目列表（管理用） | JSON 数组 |
| `products:{project_id}` | 某项目下的产品列表 | `products:oakiot` → JSON 数组 |

**免费额度**: 10万读/天，1000写/天 — 设备每日检查 1 次，支撑 10 万台设备。

### 4.3 R2（固件存储层）

**Bucket**: `oakiot-ota-firmware`
**绑定到 Worker 的变量名**: `OTA_R2`

Worker 固件下载路由直接从 R2 读取并 stream 返回：
```js
async function handleFirmwareDownload(request, env, params) {
  const { project, product, hw, file } = params;
  const objectKey = `${project}/${product}/${hw}/${file}`;
  const object = await env.OTA_R2.get(objectKey);
  if (!object) return new Response('Not Found', { status: 404 });

  return new Response(object.body, {
    headers: {
      'Content-Type': 'application/octet-stream',
      'Content-Length': object.size,
      'X-Firmware-MD5': object.customMetadata?.md5 || ''
    }
  });
}
```

**免费额度**: 10GB 存储 + 1000万 B类请求/月 + **出站流量免费**。

### 4.4 Analytics Engine（数据分析层）

**Dataset**: `ota_events`

记录所有 OTA 相关事件，用于监控设备升级情况：

| 字段 | 类型 | 说明 |
|------|------|------|
| `blob1` | string | project_id |
| `blob2` | string | product_id |
| `blob3` | string | device_id |
| `blob4` | string | 当前固件版本 |
| `blob5` | string | 目标固件版本 |
| `blob6` | string | 事件类型 |
| `double1` | number | 固件大小 / 耗时(ms) |
| `index1` | string | `{project_id}:{product_id}`（用于按项目+产品聚合查询） |

**事件类型枚举**:

| 事件 | 触发时机 |
|------|---------|
| `check_no_update` | 检查无更新 |
| `check_has_update` | 检查有更新 |
| `download_start` | 设备开始下载 |
| `download_complete` | 下载完成 |
| `update_success` | 升级成功（设备上报） |
| `update_failed` | 升级失败（设备上报） |
| `rollback` | 回滚（设备上报） |

**查询示例（Workers Analytics Engine SQL API）**:
```sql
-- 过去 7 天各项目各产品升级成功率
SELECT
  blob1 AS project_id,
  blob2 AS product_id,
  COUNT(IF(blob6 = 'update_success', 1, NULL)) AS success,
  COUNT(IF(blob6 = 'update_failed', 1, NULL)) AS failed,
  COUNT(IF(blob6 = 'rollback', 1, NULL)) AS rollback
FROM ota_events
WHERE timestamp > NOW() - INTERVAL '7' DAY
GROUP BY blob1, blob2

-- 某项目某产品的活跃设备版本分布
SELECT
  blob4 AS fw_version,
  COUNT(DISTINCT blob3) AS device_count
FROM ota_events
WHERE index1 = 'oakiot:led-light'
  AND timestamp > NOW() - INTERVAL '30' DAY
GROUP BY blob4

-- 按项目维度统计设备数
SELECT
  blob1 AS project_id,
  COUNT(DISTINCT blob3) AS total_devices
FROM ota_events
WHERE timestamp > NOW() - INTERVAL '30' DAY
GROUP BY blob1
```

**免费额度**: 10万事件/天（每设备每天最多 3-5 个事件，支撑 2-3 万台设备）。

---

## 五、ESP32 固件端设计

### 5.1 新增模块

**新建文件**: `ota_service.h` / `ota_service.cpp`

### 5.2 OTA 状态机

```
  IDLE ──(定时器/手动触发)──→ CHECKING
    ↑                            ↓
    ←── NO_UPDATE ←──────── (无更新)
    ↑                            ↓
    ←── AVAILABLE ←──────── (有更新，等待用户确认)
    ↑        ↓
    ↑    (用户确认)
    ↑        ↓
    ↑    DOWNLOADING ──→ VERIFYING ──→ REBOOTING
    ↑        ↓                ↓
    ←── ERROR ←──────────── (失败)
```

**状态码定义**:

| 状态 | 值 | 说明 |
|------|---|------|
| OTA_IDLE | 0x00 | 空闲 |
| OTA_CHECKING | 0x01 | 正在检查更新 |
| OTA_AVAILABLE | 0x02 | 有新版本可用 |
| OTA_NO_UPDATE | 0x03 | 已是最新版本 |
| OTA_DOWNLOADING | 0x04 | 正在下载固件 |
| OTA_VERIFYING | 0x05 | 正在校验 |
| OTA_READY | 0x06 | 校验通过，准备重启 |
| OTA_REBOOTING | 0x07 | 即将重启 |
| OTA_SUCCESS | 0x08 | 更新成功（重启后） |
| OTA_ERR_HTTP | 0xE0 | HTTP 请求失败 |
| OTA_ERR_PARSE | 0xE1 | 响应解析失败 |
| OTA_ERR_DOWNLOAD | 0xE2 | 下载失败 |
| OTA_ERR_MD5 | 0xE3 | MD5 校验失败 |
| OTA_ERR_FLASH | 0xE4 | 写入 Flash 失败 |
| OTA_ERR_NO_WIFI | 0xE5 | WiFi 未连接 |

### 5.3 模块接口设计（已实现）

`ota_service.h` 设计为零项目依赖的独立模块，仅通过编译宏配置，便于跨项目复用。

**编译时必需宏**（未定义时 `#error` 报错）：

| 宏名 | 类型 | 示例 | 说明 |
|------|------|------|------|
| `OTA_PROJECT_ID` | string | `"oakiot"` | 项目/客户标识 |
| `OTA_PRODUCT_ID` | string | `"moon-light"` | 产品标识 |
| `OTA_HW_VERSION` | string | `"esp32c3-v1"` | 硬件版本 |
| `OTA_FW_VERSION` | string | `"1.0.0"` | 当前固件版本（语义化） |
| `OTA_BASE_URL` | string | `"https://ota.iot.oakiot.cc"` | OTA 服务器根地址 |

**可选宏**（有默认值）：

| 宏名 | 默认值 | 说明 |
|------|--------|------|
| `OTA_CHECK_INTERVAL` | `86400000UL` (24h) | 自动检查间隔 (ms) |
| `OTA_FIRST_DELAY` | `60000UL` (60s) | 启动后首次检查延迟 (ms) |
| `OTA_API_PATH` | `"/api/v1/ota"` | API 路径前缀 |

**更新信息结构**：
```cpp
struct OtaUpdateInfo {
  bool     available;   // 是否有可用更新
  String   version;     // 新版本号
  String   url;         // 固件下载地址（Worker 返回的完整 URL）
  String   md5;         // 固件 MD5（32 字符 hex）
  String   changelog;   // 更新日志
  uint32_t size;        // 固件大小（字节）
  bool     force;       // 是否强制更新
};
```

**状态回调**：
```cpp
// callback(state, progress) — state: 状态码, progress: 0-100
typedef void (*OtaStateCallback)(uint8_t state, uint8_t progress);
void otaSetStateCallback(OtaStateCallback cb);
```

**公开函数**：

| 函数 | 调用时机 | 说明 |
|------|---------|------|
| `initOtaService()` | `setup()` 中，WiFi 初始化后 | 初始化状态、恢复 NVS 更新信息 |
| `loopOtaService(bool wifiConnected)` | `loop()` 中 | 定时自动检查，传入 WiFi 状态解耦依赖 |
| `otaConfirmIfNeeded()` | `setup()` 中 | OTA 后首次启动确认分区有效，防止回滚循环 |
| `otaCheckNow()` | BLE/Web/串口触发 | 手动立即检查更新 |
| `otaStartUpdate()` | 用户确认后调用 | 开始下载固件并写入 Flash |
| `otaCancelUpdate()` | 用户取消 | 取消操作回到 IDLE（下载中不可取消） |
| `getOtaState()` | 查询 | 返回当前状态码 |
| `getOtaProgress()` | 查询 | 返回下载进度 0-100 |
| `getOtaCurrentVersion()` | 查询 | 返回 `OTA_FW_VERSION` 字符串 |
| `getOtaUpdateInfo()` | 查询 | 返回可用更新信息结构引用 |

### 5.4 BLE OTA 服务（已实现）

新增一个 BLE GATT 服务用于 OTA 交互，集成在 `ble_service.cpp` 中。

**OTA 服务 UUID**: `0000ff30-0000-1000-8000-00805f9b34fb`

| 特征值 | UUID | 属性 | 格式 | 说明 |
|--------|------|------|------|------|
| OTA Control | `0000ff31-0000-1000-8000-00805f9b34fb` | Write / Notify | 1 字节 | 控制命令写入 & 状态码通知 |
| OTA Info | `0000ff32-0000-1000-8000-00805f9b34fb` | Read / Notify | 变长 JSON | 版本信息 + 更新详情 |
| OTA Progress | `0000ff33-0000-1000-8000-00805f9b34fb` | Read / Notify | 2 字节 | [进度%, 状态码] |

#### OTA Control 写入命令

| 命令 | 值 | 说明 | 发送后设备行为 |
|------|---|------|---------------|
| 检查更新 | `0x01` | 手动触发检查 | 立即向服务器发 POST /check，结果通过 Notify 推送 |
| 确认升级 | `0x02` | 用户确认开始下载 | 状态切到 DOWNLOADING，开始流式写入 |
| 取消 | `0x03` | 取消当前操作 | 回到 IDLE（下载/写入中不可取消） |

#### OTA Control 通知（设备 → 客户端）

状态变化时自动 Notify，1 字节当前状态码。客户端订阅此特征值即可实时感知 OTA 流程。

典型通知序列（一次完整升级）：
```
0x01 (CHECKING) → 0x02 (AVAILABLE) → 0x04 (DOWNLOADING) → 0x05 (VERIFYING) → 0x06 (READY) → 0x07 (REBOOTING)
```

#### OTA Progress 通知（设备 → 客户端）

下载过程中每变化 1% Notify 一次，固定 2 字节：

| 字节 | 说明 | 范围 |
|------|------|------|
| [0] | 下载进度百分比 | 0-100 |
| [1] | 当前状态码 | 见 5.2 状态码表 |

**Web 解析示例（JavaScript）**：
```javascript
charProgress.addEventListener('characteristicvaluechanged', (e) => {
  const data = new Uint8Array(e.target.value.buffer);
  const progress = data[0];  // 0-100
  const state = data[1];     // OTA 状态码
  updateProgressBar(progress);
});
```

#### OTA Info 读取响应（设备 → 客户端）

Read 或 Notify 时返回 UTF-8 JSON 字符串：

**有可用更新时**：
```json
{
  "cur": "1.0.0",
  "new": "1.2.0",
  "size": 483200,
  "log": "修复BLE断连；优化灯效",
  "force": false,
  "state": 2
}
```

**无可用更新时**：
```json
{
  "cur": "1.0.0",
  "new": "",
  "state": 0
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `cur` | string | 当前固件版本 |
| `new` | string | 可用新版本（无更新时为空串） |
| `size` | number | 固件大小(字节)，仅有更新时存在 |
| `log` | string | 更新日志，仅有更新时存在 |
| `force` | boolean | 是否强制更新，仅有更新时存在 |
| `state` | number | 当前 OTA 状态码 |

#### BLE OTA 完整交互序列

```
客户端 (Web/App)                        ESP32 设备
     │                                      │
     │── Write OTA Ctrl: 0x01 ──────────>│  手动检查更新
     │                                      │── POST /api/v1/ota/check ──> Worker
     │<── Notify OTA Ctrl: 0x01 ─────────│  (CHECKING)
     │                                      │<── 200 { update: true } ──── Worker
     │<── Notify OTA Ctrl: 0x02 ─────────│  (AVAILABLE)
     │                                      │
     │── Read OTA Info ─────────────────>│
     │<── { cur, new, size, log, ... } ──│  读取更新详情
     │                                      │
     │──（用户确认）──────────────────────│
     │── Write OTA Ctrl: 0x02 ──────────>│  确认开始升级
     │<── Notify OTA Ctrl: 0x04 ─────────│  (DOWNLOADING)
     │<── Notify Progress: [1%, 0x04] ───│
     │<── Notify Progress: [2%, 0x04] ───│
     │         ... 每变化1%通知 ...        │── GET firmware URL ──> Worker
     │<── Notify Progress: [100%, 0x04] ─│
     │<── Notify OTA Ctrl: 0x05 ─────────│  (VERIFYING)
     │<── Notify OTA Ctrl: 0x06 ─────────│  (READY)
     │<── Notify OTA Ctrl: 0x07 ─────────│  (REBOOTING)
     │                                      │── ESP.restart()
     │         ... BLE 断开 ...             │
     │         ... 重连后 ...               │
     │── Read OTA Info ─────────────────>│  确认新版本
     │<── { cur: "1.2.0", new: "" } ─────│
```

### 5.5 串口命令扩展（已实现）

扩展现有 JSON 串口接口（通过 Serial 发送 JSON，设备回复 JSON）：

#### ota_check — 手动检查更新

**请求**：
```json
{ "cmd": "ota_check" }
```
**响应**：
```json
{ "resp": "ota_status", "state": 2, "cur": "1.0.0", "new": "1.2.0", "size": 483200 }
```

#### ota_update — 确认开始升级

**请求**：
```json
{ "cmd": "ota_update" }
```
**响应**：
```json
{ "resp": "ota_status", "state": 4, "progress": 0 }
```

#### ota_status — 查询当前 OTA 状态

**请求**：
```json
{ "cmd": "ota_status" }
```
**响应（无更新）**：
```json
{ "resp": "ota_status", "state": 0, "progress": 0, "cur": "1.0.0" }
```
**响应（有可用更新）**：
```json
{ "resp": "ota_status", "state": 2, "progress": 0, "cur": "1.0.0", "new": "1.2.0", "size": 483200, "changelog": "修复BLE断连" }
```

#### ota_cancel — 取消操作

**请求**：
```json
{ "cmd": "ota_cancel" }
```
**响应**：
```json
{ "resp": "ota_status", "state": 0 }
```

> **注意**：串口响应中 `state` 为数值状态码（0x00-0xFF），见 5.2 状态码表。

### 5.6 自动检查逻辑（已实现）

`loopOtaService(bool wifiConnected)` 在主循环中调用，由调用方传入 WiFi 连接状态（解耦 WiFi 模块依赖）。

**实际行为**：

| 阶段 | 条件 | 行为 |
|------|------|------|
| 首次检查 | 启动后等待 `OTA_FIRST_DELAY`(60s) | 若 NVS 中无待更新 → 触发检查；有待更新 → 直接恢复 AVAILABLE 状态 |
| 定期检查 | 距上次检查 ≥ `OTA_CHECK_INTERVAL`(24h) | 仅在 IDLE 状态 & WiFi 已连接时触发 |
| 跳过检查 | 正在下载/校验/重启中 | loopOtaService 直接 return |

**NVS 持久化**：检查到新版本后将 `OtaUpdateInfo` 存入 NVS(`ota_cfg` 命名空间)。设备重启后自动恢复，无需重复 HTTP 检查。

**版本比较**：设备端使用语义化版本比较（Major.Minor.Patch），即使服务端误报也会在客户端二次校验，只接受严格大于当前版本的更新。

### 5.7 固件下载流程（已实现）

`otaStartUpdate()` 触发 `doDownloadAndFlash()` 内部函数，执行阻塞式流式下载。

**下载流程**：

```
1. 校验状态 == AVAILABLE && URL 非空
2. 设置 MD5: Update.setMD5(md5)（32 字符 hex）
3. Update.begin(size)
4. HTTPS GET 下载固件（WiFiClientSecure + setInsecure()）
5. 1024 字节缓冲区循环读写 Flash
6. 每变化 1% 触发 stateCallback → BLE Notify
7. Update.end(true) → 自动 MD5 校验
8. 清除 NVS 更新信息
9. reportResult("update_success") → POST /report
10. delay(1000) → ESP.restart()
```

**HTTPS 配置**：
- 使用 `WiFiClientSecure` + `client.setInsecure()`（Cloudflare Workers 使用受信 CA，ESP32 不用自行验证证书链）
- 下载超时：30 秒
- 缓冲区：1024 字节

**错误处理**：

| 错误 | 状态码 | 时机 |
|------|--------|------|
| `Update.begin()` 失败 | `0xE4` ERR_FLASH | Flash 空间不足或分区异常 |
| HTTP GET 非 200 | `0xE2` ERR_DOWNLOAD | 网络/服务器错误 |
| `Update.write()` 返回不一致 | `0xE4` ERR_FLASH | Flash 写入异常 |
| `Update.end()` 校验失败 | `0xE3` ERR_MD5 | 传输损坏或被篡改 |

> **注意**：下载过程是阻塞的（loop 被占用），期间 BLE/LED 等更新暂停。设计上可接受，因固件通常 <500KB，在良好网络下 10-30 秒完成。

### 5.8 启动回滚保护（已实现）

`otaConfirmIfNeeded()` 在 `setup()` 中调用，检查当前分区状态：

```
OTA 写入新固件 → 切换到新分区 → 重启
                                    ↓
                          分区状态: PENDING_VERIFY
                                    ↓
            otaConfirmIfNeeded() → esp_ota_mark_app_valid_cancel_rollback()
                                    ↓
                          分区状态: VALID（确认成功）
```

**回滚机制**：若新固件崩溃（无法执行到 `otaConfirmIfNeeded()`），ESP32 bootloader 会自动回滚到上一个有效分区。

**分区信息日志**：启动时打印 `[OTA] 运行分区: ota_0 (地址: 0x010000)`。

---

## 六、现有代码集成改动

### 6.1 config.h 新增（已实现）

```cpp
// ============ 固件版本 & OTA 配置 ============
#define OTA_PROJECT_ID      "oakiot"           // 项目/客户标识
#define OTA_PRODUCT_ID      "moon-light"        // 产品标识
#define OTA_FW_VERSION      "1.0.0"            // 固件版本号
#define OTA_HW_VERSION      "esp32c3-v1"       // 硬件版本
#define OTA_BASE_URL        "https://ota.iot.oakiot.cc"
#define OTA_CHECK_INTERVAL  86400000UL         // 自动检查间隔: 24小时 (ms)
#define OTA_FIRST_DELAY     60000UL            // 首次检查延迟: 60秒 (ms)

// ============ OTA BLE 服务 UUID ============
#define OTA_SERVICE_UUID        "0000ff30-0000-1000-8000-00805f9b34fb"
#define CHAR_OTA_CTRL_UUID      "0000ff31-0000-1000-8000-00805f9b34fb"
#define CHAR_OTA_INFO_UUID      "0000ff32-0000-1000-8000-00805f9b34fb"
#define CHAR_OTA_PROGRESS_UUID  "0000ff33-0000-1000-8000-00805f9b34fb"

// OTA BLE 特征值指针
extern BLECharacteristic *pCharOtaCtrl;
extern BLECharacteristic *pCharOtaInfo;
extern BLECharacteristic *pCharOtaProgress;
```

> **复用说明**：所有宏名统一 `OTA_` 前缀（而非 `PROJECT_ID`），避免与项目其他宏冲突。新项目只需修改这些宏值。

### 6.2 ble_service.cpp 新增

在 `setupBLE()` 中添加 OTA Service 的创建逻辑（与现有 LED/WiFi/Time 服务并列）：
  - 创建 OTA Service (`0000ff30`)
  - 创建 3 个特征值 (Ctrl/Info/Progress)
  - 注册回调

### 6.3 ESP32_LED_TEST.ino 改动（已实现）

```cpp
#include "ota_service.h"

// 全局定义 OTA BLE 特征值指针
BLECharacteristic *pCharOtaCtrl = nullptr;
BLECharacteristic *pCharOtaInfo = nullptr;
BLECharacteristic *pCharOtaProgress = nullptr;

// OTA 状态变化回调 → BLE Notify
static void onOtaStateChange(uint8_t state, uint8_t progress) {
  if (!deviceConnected) return;
  if (pCharOtaCtrl) { pCharOtaCtrl->setValue(&state, 1); pCharOtaCtrl->notify(); }
  if (pCharOtaProgress) {
    uint8_t buf[2] = { progress, state };
    pCharOtaProgress->setValue(buf, 2);
    pCharOtaProgress->notify();
  }
}

// setup() 中，WiFi/Time 初始化之后、setupBLE() 之前:
  initOtaService();
  otaSetStateCallback(onOtaStateChange);
  otaConfirmIfNeeded();

// loop() 中:
  loopOtaService(isWiFiConnected());

// 串口命令: ota_check, ota_update, ota_status, ota_cancel
// get_device_info 响应增加: project_id, product_id, fw_version, hw_version
```

### 6.4 NVS 存储（已实现）

OTA 模块使用独立 NVS 命名空间 `ota_cfg`，在 `ota_service.cpp` 内部管理（不依赖 storage.cpp）：

| 键名 | 类型 | 说明 |
|------|------|------|
| `hasUpdate` | Bool | 是否有待更新 |
| `newVer` | String | 待更新版本号 |
| `newUrl` | String | 待更新下载地址 |
| `newMd5` | String | 待更新 MD5 |
| `newLog` | String | 更新日志 |
| `newSize` | ULong | 待更新固件大小 |
| `force` | Bool | 是否强制更新 |

**持久化时机**：
- 检查到新版本时 → `saveUpdateInfoToNVS()`
- 下载成功后 → `clearUpdateInfoNVS()`
- 用户取消时 → `clearUpdateInfoNVS()`
- 启动时版本已匹配 → `clearUpdateInfoNVS()`

> 持久化更新信息的目的：设备重启后仍能提示用户有待更新，无需再次 HTTP 检查。

---

## 七、Cloudflare Worker 完整实现计划

### 7.1 项目结构

```
cloudflare-ota-worker/
├── wrangler.toml               # Worker 配置（KV/R2/AE 绑定）
├── src/
│   ├── index.js                # 路由入口
│   ├── routes/
│   │   ├── check.js            # POST /api/v1/ota/check
│   │   ├── firmware.js         # GET  /api/v1/ota/firmware/...
│   │   ├── report.js           # POST /api/v1/ota/report
│   │   └── admin.js            # /admin/* 管理后台 API
│   ├── admin/
│   │   ├── index.html          # 管理后台 SPA 页面
│   │   ├── admin.css           # 后台样式
│   │   └── admin.js            # 后台前端逻辑
│   ├── middleware/
│   │   └── auth.js             # 管理后台认证中间件
│   └── utils/
│       ├── version.js          # 语义化版本比较
│       └── analytics.js        # Analytics Engine 事件记录
└── package.json
```

### 7.2 wrangler.toml 配置

```toml
name = "oakiot-ota"
main = "src/index.js"
compatibility_date = "2026-03-01"

routes = [
  { pattern = "ota.iot.oakiot.cc/*", zone_name = "oakiot.cc" }
]

[vars]
ADMIN_USERNAME = "admin"          # 管理后台用户名（明文，非敏感）

# ADMIN_PASSWORD_HASH 通过 secret 设置，不写入代码仓库
# 使用 wrangler secret put ADMIN_PASSWORD_HASH 命令设置
# 值为 SHA-256(password) 的 hex 字符串

[[kv_namespaces]]
binding = "OTA_KV"
id = "xxxxxxxxxxxxxxxxxxxx"

[[r2_buckets]]
binding = "OTA_R2"
bucket_name = "oakiot-ota-firmware"

[[analytics_engine_datasets]]
binding = "OTA_ANALYTICS"
dataset = "ota_events"
```

### 7.3 固件发版流程

**方式一：管理后台（推荐）**

通过 `https://ota.iot.oakiot.cc/admin` Web 管理后台上传发布，详见 7.4 节。

**方式二：Wrangler CLI**

适用于 CI/CD 流水线或开发者本地发版：

```bash
#!/bin/bash
# publish-firmware.sh
# 用法: ./publish-firmware.sh oakiot led-light esp32c3-v1 1.2.0 ./build/firmware.bin

PROJECT=$1
PRODUCT=$2
HW=$3
VERSION=$4
FIRMWARE=$5
MD5=$(md5sum "$FIRMWARE" | awk '{print $1}')
SIZE=$(stat -c%s "$FIRMWARE")

# 1. 上传固件到 R2
wrangler r2 object put "oakiot-ota-firmware/${PROJECT}/${PRODUCT}/${HW}/firmware-${VERSION}.bin" \
  --file="$FIRMWARE" \
  --content-type="application/octet-stream" \
  --md5="$MD5"

# 2. 更新 KV 元数据
cat <<EOF | wrangler kv:key put "latest:${PROJECT}:${PRODUCT}:${HW}" --namespace-id=YOUR_KV_ID --stdin
{
  "version": "${VERSION}",
  "build_ts": $(date +%s),
  "size": ${SIZE},
  "md5": "${MD5}",
  "min_version": "1.0.0",
  "force": false,
  "changelog": "版本 ${VERSION} 更新",
  "filename": "firmware-${VERSION}.bin",
  "updated_at": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
EOF

echo "✅ 固件 ${PROJECT}/${PRODUCT}/${HW} v${VERSION} 发布完成 (MD5: ${MD5})"
```

### 7.4 固件管理后台

#### 7.4.1 概述

Worker 内嵌一个轻量 Web 管理后台，用于可视化管理固件。无需额外部署前端项目，HTML/CSS/JS 直接由 Worker 返回（内联在代码中或作为静态资源）。

**入口地址**: `https://ota.iot.oakiot.cc/admin`

#### 7.4.2 认证方案

采用 **用户名 + 密码** 的简单认证，通过 Session Token（JWT）维持登录状态：

```
访问 /admin → 未认证 → 显示登录页
                         ↓
          输入用户名 + 密码 → POST /admin/api/login
                                    ↓
                    Worker 校验 SHA-256(password) == ADMIN_PASSWORD_HASH
                                    ↓
                         ✓ → 签发 JWT (有效期 24h)，Set-Cookie
                         ✗ → 返回 401
                                    ↓
           后续请求携带 Cookie → 中间件验证 JWT → 放行/拒绝
```

**密码存储**：
- 密码哈希通过 `wrangler secret put ADMIN_PASSWORD_HASH` 设置为 Worker Secret
- Worker 中仅比较哈希，永远不存储或传输明文密码
- JWT 签名密钥通过 `wrangler secret put JWT_SECRET` 设置（随机 256 位字符串）

**设置密码命令**：
```bash
# 生成密码哈希
echo -n "your-password" | sha256sum | awk '{print $1}'

# 写入 Worker Secret
wrangler secret put ADMIN_PASSWORD_HASH
# 粘贴上一步的 hash 值

wrangler secret put JWT_SECRET
# 粘贴一个随机字符串，如: openssl rand -hex 32
```

**认证中间件伪代码**：
```js
// middleware/auth.js
export async function requireAuth(request, env) {
  const cookie = request.headers.get('Cookie') || '';
  const token = parseCookie(cookie, 'ota_session');
  if (!token) return null;

  try {
    const payload = await verifyJWT(token, env.JWT_SECRET);
    if (payload.exp < Date.now() / 1000) return null;
    return payload;
  } catch {
    return null;
  }
}

export async function handleLogin(request, env) {
  const { username, password } = await request.json();
  const hash = await sha256(password);

  if (username !== env.ADMIN_USERNAME || hash !== env.ADMIN_PASSWORD_HASH) {
    return json({ error: 'Invalid credentials' }, 401);
  }

  const token = await signJWT({ sub: username, exp: Math.floor(Date.now()/1000) + 86400 }, env.JWT_SECRET);
  return json({ ok: true }, 200, {
    'Set-Cookie': `ota_session=${token}; Path=/admin; HttpOnly; Secure; SameSite=Strict; Max-Age=86400`
  });
}
```

#### 7.4.3 管理后台路由表

| 方法 | 路径 | 认证 | 功能 |
|------|------|------|------|
| GET | `/admin` | 否 | 返回管理后台 HTML 页面（含登录） |
| POST | `/admin/api/login` | 否 | 登录认证，返回 JWT Cookie |
| POST | `/admin/api/logout` | 否 | 清除Cookie |
| GET | `/admin/api/projects` | ✅ | 获取所有项目列表 |
| POST | `/admin/api/projects` | ✅ | 创建新项目 |
| DELETE | `/admin/api/projects/:project` | ✅ | 删除项目（及其下所有产品/固件） |
| GET | `/admin/api/projects/:project/products` | ✅ | 获取项目下的产品列表 |
| POST | `/admin/api/projects/:project/products` | ✅ | 创建新产品 |
| DELETE | `/admin/api/projects/:project/products/:product` | ✅ | 删除产品 |
| GET | `/admin/api/firmware/:project/:product/:hw` | ✅ | 获取某产品某硬件的所有固件版本 |
| GET | `/admin/api/firmware/:project/:product/:hw/latest` | ✅ | 获取最新版本元数据 |
| POST | `/admin/api/firmware/:project/:product/:hw` | ✅ | 上传新固件 + 更新元数据 |
| PUT | `/admin/api/firmware/:project/:product/:hw/latest` | ✅ | 修改最新版本元数据（changelog/force/min_version） |
| DELETE | `/admin/api/firmware/:project/:product/:hw/:version` | ✅ | 删除指定版本固件 |
| GET | `/admin/api/stats` | ✅ | 获取全局统计（设备数/检查数/更新数） |
| GET | `/admin/api/stats/:project/:product` | ✅ | 获取特定产品统计 |

#### 7.4.4 管理后台页面设计

单页应用（SPA），HTML 内嵌在 Worker 中返回。页面结构如下：

```
┌─────────────────────────────────────────────────────────┐
│  🔧 OTA 固件管理后台                    [admin] [登出]  │
├──────────┬──────────────────────────────────────────────┤
│          │                                              │
│  项目列表  │  面包屑: oakiot > led-light > esp32c3-v1    │
│          │                                              │
│  ◉ oakiot │  ┌─ 当前发布版本 ─────────────────────────┐ │
│    ├ led- │  │ v1.2.0  |  483KB  |  2026-03-14        │ │
│    │ light│  │ MD5: a1b2c3d4...                       │ │
│    │  ├c3 │  │ 最低版本: 1.0.0  |  强制: 否           │ │
│    │  └c3 │  │ 更新日志: 修复BLE断连；优化灯效        │ │
│    ├ sens │  │ [编辑元数据]                            │ │
│    └ gate │  └────────────────────────────────────────┘ │
│  ◯ cliA  │                                              │
│  ◯ cliB  │  ┌─ 上传新固件 ───────────────────────────┐ │
│          │  │ 版本号: [1.3.0    ]                     │ │
│ [+新项目]│  │ 固件文件: [选择 .bin 文件]   📎         │ │
│          │  │ 更新日志: [________________]             │ │
│          │  │ 最低版本: [1.0.0  ]  □ 强制更新         │ │
│          │  │ [上传并发布]                             │ │
│          │  └────────────────────────────────────────┘ │
│          │                                              │
│          │  ┌─ 历史版本 ─────────────────────────────┐ │
│          │  │ v1.2.0  483KB  2026-03-14  [当前] [删除]│ │
│          │  │ v1.1.0  480KB  2026-03-01         [删除]│ │
│          │  │ v1.0.0  475KB  2026-02-15         [删除]│ │
│          │  └────────────────────────────────────────┘ │
│          │                                              │
│          │  ┌─ 设备统计 (Analytics) ─────────────────┐ │
│          │  │ 活跃设备: 127台  |  今日检查: 125次     │ │
│          │  │ 版本分布: v1.2.0(80%) v1.1.0(15%) ...   │ │
│          │  └────────────────────────────────────────┘ │
└──────────┴──────────────────────────────────────────────┘
```

**页面功能分区**：

| 区域 | 功能 |
|------|------|
| 左侧栏 | 项目→产品→硬件 三级树形导航；底部 [+新项目] 按钮 |
| 面包屑 | 展示当前选中的 project > product > hw_version 路径 |
| 当前发布版本 | 显示 KV 中最新版本的详细元数据，可编辑 |
| 上传新固件 | 表单：版本号 + .bin 文件 + changelog + min_version + force 开关 |
| 历史版本 | 列出 R2 中该路径下所有固件文件，支持删除 |
| 设备统计 | 从 Analytics Engine 查询并展示设备/版本分布数据 |

#### 7.4.5 核心 API 实现细节

**上传固件 — `POST /admin/api/firmware/:project/:product/:hw`**

接收 `multipart/form-data`，包含固件二进制和元数据：

```js
async function handleUploadFirmware(request, env, { project, product, hw }) {
  const formData = await request.formData();
  const file = formData.get('firmware');       // File 对象
  const version = formData.get('version');     // "1.3.0"
  const changelog = formData.get('changelog'); // "修复xxx"
  const minVersion = formData.get('min_version') || '1.0.0';
  const force = formData.get('force') === 'true';

  if (!file || !version) return json({ error: 'Missing firmware or version' }, 400);

  // 版本格式校验
  if (!/^\d+\.\d+\.\d+$/.test(version)) {
    return json({ error: 'Invalid version format, expected x.y.z' }, 400);
  }

  // 读取文件内容并计算 MD5
  const arrayBuffer = await file.arrayBuffer();
  const md5 = await computeMD5(arrayBuffer);
  const size = arrayBuffer.byteLength;
  const filename = `firmware-${version}.bin`;
  const r2Key = `${project}/${product}/${hw}/${filename}`;

  // 检查是否已存在同版本
  const existing = await env.OTA_R2.head(r2Key);
  if (existing) {
    return json({ error: `Version ${version} already exists` }, 409);
  }

  // 上传到 R2
  await env.OTA_R2.put(r2Key, arrayBuffer, {
    httpMetadata: { contentType: 'application/octet-stream' },
    customMetadata: { version, md5, project, product, hw }
  });

  // 更新 KV 最新版本元数据
  const kvKey = `latest:${project}:${product}:${hw}`;
  const meta = {
    version,
    build_ts: Math.floor(Date.now() / 1000),
    size,
    md5,
    min_version: minVersion,
    force,
    changelog: changelog || `版本 ${version} 更新`,
    filename,
    updated_at: new Date().toISOString()
  };
  await env.OTA_KV.put(kvKey, JSON.stringify(meta));

  // 更新产品注册信息（确保产品在列表中）
  await ensureRegistered(env, project, product, hw);

  // 记录 Analytics 事件
  env.OTA_ANALYTICS.writeDataPoint({
    blobs: [project, product, 'admin', version, '', 'firmware_published'],
    doubles: [size],
    indexes: [`${project}:${product}`]
  });

  return json({ ok: true, version, size, md5, r2Key });
}
```

**删除固件 — `DELETE /admin/api/firmware/:project/:product/:hw/:version`**

```js
async function handleDeleteFirmware(request, env, { project, product, hw, version }) {
  const filename = `firmware-${version}.bin`;
  const r2Key = `${project}/${product}/${hw}/${filename}`;

  // 检查是否为当前发布版本，阻止删除正在使用的版本
  const kvKey = `latest:${project}:${product}:${hw}`;
  const latest = await env.OTA_KV.get(kvKey, 'json');
  if (latest && latest.version === version) {
    return json({ error: 'Cannot delete the currently published version. Publish a new version first.' }, 400);
  }

  // 从 R2 删除
  await env.OTA_R2.delete(r2Key);

  return json({ ok: true, deleted: r2Key });
}
```

**修改元数据 — `PUT /admin/api/firmware/:project/:product/:hw/latest`**

允许修改 changelog、force、min_version 等字段，不重新上传固件：

```js
async function handleUpdateMeta(request, env, { project, product, hw }) {
  const kvKey = `latest:${project}:${product}:${hw}`;
  const meta = await env.OTA_KV.get(kvKey, 'json');
  if (!meta) return json({ error: 'No firmware found' }, 404);

  const updates = await request.json();
  // 仅允许修改安全字段
  const allowed = ['changelog', 'force', 'min_version'];
  for (const key of allowed) {
    if (updates[key] !== undefined) meta[key] = updates[key];
  }
  meta.updated_at = new Date().toISOString();

  await env.OTA_KV.put(kvKey, JSON.stringify(meta));
  return json({ ok: true, meta });
}
```

**列出历史版本 — `GET /admin/api/firmware/:project/:product/:hw`**

扫描 R2 Bucket 中该路径下的所有固件文件：

```js
async function handleListFirmware(request, env, { project, product, hw }) {
  const prefix = `${project}/${product}/${hw}/`;
  const listed = await env.OTA_R2.list({ prefix });

  const kvKey = `latest:${project}:${product}:${hw}`;
  const latest = await env.OTA_KV.get(kvKey, 'json');

  const versions = listed.objects.map(obj => {
    // 从文件名提取版本号: firmware-1.2.0.bin → 1.2.0
    const match = obj.key.match(/firmware-([\d.]+)\.bin$/);
    return {
      version: match ? match[1] : obj.key,
      size: obj.size,
      uploaded: obj.uploaded,
      md5: obj.customMetadata?.md5 || '',
      is_latest: latest && match && latest.version === match[1]
    };
  });

  // 按版本号降序排列
  versions.sort((a, b) => compareVersions(b.version, a.version));

  return json({ versions, latest: latest || null });
}
```

**项目产品注册管理**

使用 KV 维护项目→产品→硬件的树形注册关系：

```js
// 确保项目/产品/硬件已注册到索引中
async function ensureRegistered(env, project, product, hw) {
  // 注册项目
  const projects = await env.OTA_KV.get('projects', 'json') || [];
  if (!projects.includes(project)) {
    projects.push(project);
    await env.OTA_KV.put('projects', JSON.stringify(projects));
  }

  // 注册产品
  const prodKey = `products:${project}`;
  const products = await env.OTA_KV.get(prodKey, 'json') || [];
  if (!products.includes(product)) {
    products.push(product);
    await env.OTA_KV.put(prodKey, JSON.stringify(products));
  }

  // 注册硬件版本
  const hwKey = `hw_versions:${project}:${product}`;
  const hwVersions = await env.OTA_KV.get(hwKey, 'json') || [];
  if (!hwVersions.includes(hw)) {
    hwVersions.push(hw);
    await env.OTA_KV.put(hwKey, JSON.stringify(hwVersions));
  }
}
```

**KV 索引结构（完整）**：

| Key | 值类型 | 说明 |
|-----|--------|------|
| `projects` | JSON 数组 | 所有项目 ID 列表：`["oakiot", "clientA"]` |
| `products:{project}` | JSON 数组 | 某项目下的产品列表：`["led-light", "sensor-hub"]` |
| `hw_versions:{project}:{product}` | JSON 数组 | 某产品的硬件版本：`["esp32c3-v1", "esp32c3-v2"]` |
| `latest:{project}:{product}:{hw}` | JSON 对象 | 最新固件元数据（见 2.3 节） |

#### 7.4.6 管理后台前端实现要点

**技术选择**：纯 HTML + CSS + Vanilla JS（无框架），内嵌在 Worker 代码中。

原因：
- Worker 单文件部署，无需额外 CDN / 静态托管
- 管理后台功能简单，不需要 React/Vue 等框架
- 总大小控制在 50KB 以内，首屏秒开

**前端核心交互**：

```js
// admin.js 核心结构（简化）

// 全局状态
let currentProject = null;
let currentProduct = null;
let currentHw = null;

// 初始化：加载项目树
async function init() {
  const resp = await fetch('/admin/api/projects');
  if (resp.status === 401) { showLogin(); return; }
  const { projects } = await resp.json();
  renderProjectTree(projects);
}

// 上传固件
async function uploadFirmware() {
  const form = new FormData();
  form.append('firmware', document.getElementById('fw-file').files[0]);
  form.append('version', document.getElementById('fw-version').value);
  form.append('changelog', document.getElementById('fw-changelog').value);
  form.append('min_version', document.getElementById('fw-min-ver').value);
  form.append('force', document.getElementById('fw-force').checked);

  const resp = await fetch(
    `/admin/api/firmware/${currentProject}/${currentProduct}/${currentHw}`,
    { method: 'POST', body: form }
  );
  const result = await resp.json();
  if (result.ok) {
    showToast(`固件 v${result.version} 发布成功 (${formatBytes(result.size)})`);
    await refreshVersionList();
  } else {
    showToast(`上传失败: ${result.error}`, 'error');
  }
}

// 删除固件
async function deleteFirmware(version) {
  if (!confirm(`确认删除固件 v${version}？此操作不可撤销。`)) return;
  await fetch(
    `/admin/api/firmware/${currentProject}/${currentProduct}/${currentHw}/${version}`,
    { method: 'DELETE' }
  );
  await refreshVersionList();
}
```

**上传进度展示**：使用 `XMLHttpRequest` 的 `upload.onprogress` 事件显示大文件上传进度条（固件通常 <500KB，基本秒传）。

**安全注意事项**：
- 所有 `/admin/api/*` 请求在 Worker 端过认证中间件
- 前端 JS 仅操作 DOM，不存储密码
- Cookie 设置 `HttpOnly; Secure; SameSite=Strict` 防止 XSS/CSRF
- 删除操作需二次确认
- 不允许删除当前发布的版本（防止设备下载 404）

#### 7.4.7 管理后台响应式设计

后台需在手机端也能操作（应急场景），采用简单响应式：

| 屏幕宽度 | 布局 |
|----------|------|
| ≥768px | 左右分栏（树导航 + 内容区） |
| <768px | 单栏，树导航折叠为顶部下拉菜单 |

---

## 八、安全设计

| 措施 | 实现层 | 说明 |
|------|--------|------|
| **HTTPS** | 传输层 | Worker + R2 默认 HTTPS，ESP32 使用 WiFiClientSecure |
| **MD5 校验** | 固件层 | `Update.setMD5()` 写入前自动校验，防止传输损坏/篡改 |
| **分区回滚** | 固件层 | `esp_ota_mark_app_valid_cancel_rollback()` 确认启动成功 |
| **最低版本** | Worker 层 | `min_version` 阻止老旧固件绕过中间必要更新 |
| **项目+产品隔离** | API 层 | project_id + product_id 双层隔离，不同客户/产品获取各自固件 |
| **管理后台认证** | Worker 层 | SHA-256 密码哈希 + JWT Session + HttpOnly Cookie |
| **管理后台权限** | Worker 层 | 删除保护（不可删除当前发布版本）、操作二次确认 |
| **密码不入仓库** | 部署层 | 密码哈希和 JWT 密钥通过 `wrangler secret` 独立管理 |
| **设备认证(可选)** | API 层 | 请求头携带 HMAC(device_id + secret)，Worker 验签 |

---

## 九、费用评估

| 服务 | 免费额度 | 1000 台设备/日估算 |
|------|---------|------------------|
| Workers 请求 | 10万/天 | ~1000 check + ~50 download ≈ 1050 |
| KV 读 | 10万/天 | ~1000 (每次 check 读 1 次) |
| KV 写 | 1000/天 | 仅发版时写（<10次/天） |
| R2 存储 | 10GB | ~50MB（10个固件版本） |
| R2 B类请求 | 1000万/月 | ~1500/天 |
| R2 出站 | **免费** | ✅ |
| Analytics Engine | 10万事件/天 | ~3000 事件/天 |

**结论**: 万台设备以内完全免费运行。

---

## 十、开发步骤 & 里程碑

### Phase 1: 固件端 OTA 基础（ESP32）

| 步骤 | 任务 | 涉及文件 |
|------|------|----------|
| 1.1 | config.h 添加版本号、OTA 常量、BLE UUID | `config.h` |
| 1.2 | 新建 ota_service.h/cpp，实现状态机 + HTTP 检查 | `ota_service.h/cpp` |
| 1.3 | 实现流式固件下载 + MD5 校验 + 分区切换 | `ota_service.cpp` |
| 1.4 | 实现启动回滚保护 | `ota_service.cpp` |
| 1.5 | ble_service.cpp 集成 OTA GATT 服务 | `ble_service.cpp` |
| 1.6 | .ino 主文件集成 initOTA / loopOTA / 串口命令 | `ESP32_LED_TEST.ino` |
| 1.7 | 编译测试 + 基础功能验证 | - |

### Phase 2: Cloudflare 服务端（设备 API）

| 步骤 | 任务 |
|------|------|
| 2.1 | 创建 Workers 项目，配置 wrangler.toml |
| 2.2 | 实现 check API（读 KV + 版本比较） |
| 2.3 | 实现 firmware 下载代理（读 R2 + stream） |
| 2.4 | 实现 report API（写 Analytics Engine） |
| 2.5 | 创建 R2 Bucket + KV Namespace |
| 2.6 | 绑定域名 ota.iot.oakiot.cc |
| 2.7 | 编写 publish-firmware.sh 发版脚本 |

### Phase 2.5: 管理后台

| 步骤 | 任务 |
|------|------|
| 2.5.1 | 实现认证中间件（login/logout/JWT 校验） |
| 2.5.2 | 实现项目/产品/硬件 CRUD API |
| 2.5.3 | 实现固件上传 API（multipart + R2 + KV） |
| 2.5.4 | 实现固件列表/删除/元数据修改 API |
| 2.5.5 | 实现统计查询 API（Analytics Engine SQL） |
| 2.5.6 | 编写管理后台 HTML/CSS/JS 页面 |
| 2.5.7 | 设置 Worker Secrets（密码哈希 + JWT 密钥） |
| 2.5.8 | 管理后台功能测试 |

### Phase 3: 端到端联调

| 步骤 | 任务 |
|------|------|
| 3.1 | 上传测试固件到 R2，写入 KV 元数据 |
| 3.2 | ESP32 自动检查 → 发现更新 → BLE 通知 |
| 3.3 | 串口/BLE 触发下载 → 校验 → 重启 → 回滚测试 |
| 3.4 | Analytics Engine 数据验证 |
| 3.5 | 异常场景测试（断网、断电、MD5 错误、版本回退） |

### Phase 4: Web 页面 & 文档

| 步骤 | 任务 |
|------|------|
| 4.1 | 设备端 Web 页面增加 OTA 状态展示 + 升级按钮 |
| 4.2 | 更新 PROTOCOL.md 协议文档 |
| 4.3 | 更新 ble-dev-guide 开发指南 |
| 4.4 | 管理后台使用手册 |

---

## 十一、完整协议参考

> 本节汇总所有协议细节，供服务端 (Cloudflare Worker) 和 Web 前端开发参考。

### 11.1 HTTP API 协议总览

**基址址**: `https://ota.iot.oakiot.cc`
**API 前缀**: `/api/v1/ota`
**Content-Type**: `application/json`

#### POST /api/v1/ota/check — 检查更新

**请求**：
```json
{
  "project_id": "oakiot",
  "product_id": "moon-light",
  "device_id": "AA:BB:CC:DD:EE:FF",
  "fw_version": "1.0.0",
  "hw_version": "esp32c3-v1"
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `project_id` | string | ✅ | 项目标识 |
| `product_id` | string | ✅ | 产品标识 |
| `device_id` | string | ✅ | 设备 MAC 地址 (XX:XX:XX:XX:XX:XX) |
| `fw_version` | string | ✅ | 当前固件版本 (x.y.z) |
| `hw_version` | string | ✅ | 硬件版本 |

**响应 — 无更新** (200):
```json
{
  "update": false
}
```

**响应 — 有更新** (200):
```json
{
  "update": true,
  "version": "1.2.0",
  "size": 483200,
  "md5": "a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4",
  "changelog": "修复BLE断连问题；优化灯效流畅度",
  "force": false,
  "url": "https://ota.iot.oakiot.cc/api/v1/ota/firmware/oakiot/moon-light/esp32c3-v1/firmware-1.2.0.bin"
}
```

| 响应字段 | 类型 | 说明 |
|---------|------|------|
| `update` | boolean | 是否有可用更新 |
| `version` | string | 新版本号 |
| `size` | number | 固件大小（字节） |
| `md5` | string | 固件 MD5 (32 字符 hex) |
| `changelog` | string | 更新日志 (UTF-8) |
| `force` | boolean | 是否强制更新 |
| `url` | string | 固件下载完整 URL |

**Worker 逻辑**：
1. 参数校验（project_id, product_id, fw_version 必填）
2. 读 KV: `latest:{project_id}:{product_id}:{hw_version}` → JSON 元数据
3. 语义化版本比较: `meta.version > fw_version` → 有更新
4. 可选: `min_version` 检查（过低版本需先升到中间版本）
5. 写 Analytics Engine 事件
6. 返回 JSON（含 Worker 生成的完整下载 URL）

#### GET /api/v1/ota/firmware/{project}/{product}/{hw}/{filename} — 固件下载

Worker 从 R2 读取二进制流直接返回：

```
HTTP/1.1 200 OK
Content-Type: application/octet-stream
Content-Length: 483200
X-Firmware-MD5: a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4
```

**设计决策**：Worker 代理 R2 而非 302 重定向。原因：ESP32 HTTPClient 对重定向 + 二次 TLS 握手支持不稳定，单次连接更可靠。

#### POST /api/v1/ota/report — 上报事件

**请求**：
```json
{
  "project_id": "oakiot",
  "product_id": "moon-light",
  "device_id": "AA:BB:CC:DD:EE:FF",
  "event": "update_success",
  "from_version": "1.0.0",
  "to_version": "1.2.0"
}
```

| event 枚举 | 触发时机 |
|------------|----------|
| `update_success` | 固件下载完成、校验通过后（重启前发送） |
| `update_failed` | 下载或校验失败 |
| `rollback` | 设备回滚到旧版本 |

**响应**: `200 { "ok": true }` — 设备端不检查响应，尽力发送即可。

### 11.2 BLE OTA 协议总览

**OTA 服务 UUID**: `0000ff30-0000-1000-8000-00805f9b34fb`

| 特征值 | UUID | 属性 | 格式 | 说明 |
|--------|------|------|------|------|
| OTA Control | `0000ff31-0000-1000-8000-00805f9b34fb` | Write / Notify | 1 字节 | 命令写入 & 状态通知 |
| OTA Info | `0000ff32-0000-1000-8000-00805f9b34fb` | Read / Notify | 变长 JSON UTF-8 | 版本 + 更新详情 |
| OTA Progress | `0000ff33-0000-1000-8000-00805f9b34fb` | Read / Notify | 2 字节 | [进度%, 状态码] |

#### 写入命令 (OTA Control)

| 命令 | 值 | 设备行为 |
|------|---|----------|
| 检查更新 | `0x01` | 立即 POST /check，结果 Notify 0x01→0x02 或 0x01→0x03 |
| 确认升级 | `0x02` | 仅在 state=AVAILABLE(0x02) 时有效，开始下载 |
| 取消 | `0x03` | 仅在非下载/校验/重启状态时有效，回到 IDLE |

#### 状态通知 (OTA Control Notify)

Notify 1 字节状态码：

| 状态 | 值 | Web 应展示 |
|------|---|----------|
| IDLE | `0x00` | 无操作 |
| CHECKING | `0x01` | "正在检查更新..." |
| AVAILABLE | `0x02` | 显示更新详情 + 确认按钮 |
| NO_UPDATE | `0x03` | "已是最新版本" |
| DOWNLOADING | `0x04` | 显示进度条 |
| VERIFYING | `0x05` | "正在校验..." |
| READY | `0x06` | "即将重启" |
| REBOOTING | `0x07` | "设备重启中" |
| ERR_HTTP | `0xE0` | "网络请求失败" |
| ERR_PARSE | `0xE1` | "服务器响应异常" |
| ERR_DOWNLOAD | `0xE2` | "下载失败" |
| ERR_MD5 | `0xE3` | "校验失败" |
| ERR_FLASH | `0xE4` | "写入失败" |
| ERR_NO_WIFI | `0xE5` | "WiFi 未连接" |

#### 进度通知 (OTA Progress Notify)

2 字节：`[progress_percent, state_code]`

下载过程中每变化 1% Notify 一次。Web 可直接用 `data[0]` 更新进度条。

#### OTA Info 读取/通知

返回 UTF-8 JSON 字符串：

```json
{
  "cur": "1.0.0",
  "new": "1.2.0",
  "size": 483200,
  "log": "修复BLE断连；优化灯效",
  "force": false,
  "state": 2
}
```

`new` 为空串时表示无可用更新。`size`/`log`/`force` 仅在 `new` 非空时存在。

### 11.3 串口 JSON 协议总览

所有串口命令通过 `{ "cmd": "xxx" }` 发送，设备回复单行 JSON。

| 命令 | 响应 resp | 说明 |
|------|-----------|------|
| `ota_check` | `ota_status` | 触发检查，返回 state + 更新信息 |
| `ota_update` | `ota_status` | 确认升级，返回 state + progress |
| `ota_status` | `ota_status` | 查询当前状态，返回完整信息 |
| `ota_cancel` | `ota_status` | 取消操作，返回状态 |
| `get_device_info` | `device_info` | 含 project_id/product_id/fw_version/hw_version |

### 11.4 端到端升级时序图

```
Web/App            BLE            ESP32 设备            Worker             R2
  │                 │                 │                    │               │
  │  ===== 自动检查（每24h） =====  │                    │               │
  │                 │   [loopOtaService]                  │               │
  │                 │     │── POST /api/v1/ota/check ────>│               │
  │                 │     │     {project,product,         │               │
  │                 │     │      device,fw,hw}             │               │
  │                 │     │                    │─ KV.get(latest:...) │
  │                 │     │                    │─ 版本比较         │
  │                 │     │                    │─ Analytics.write  │
  │                 │     │<── 200 { update:true, ──│               │
  │                 │     │     version, url, md5 }       │               │
  │                 │     │                                │               │
  │                 │     │─ 保存到NVS                    │               │
  │                 │     │─ setState(AVAILABLE)          │               │
  │           Notify Ctrl: 0x02                             │               │
  │<─────────────────│                                │               │
  │                 │                                      │               │
  │  ===== 用户查看更新详情 =====                      │               │
  │─ Read OTA Info ─>│                                      │               │
  │<─ {cur,new,size,log} │                                │               │
  │  显示更新弹窗   │                                      │               │
  │                 │                                      │               │
  │  ===== 用户确认升级 =====                              │               │
  │─ Write Ctrl:0x02>│───>│                                │               │
  │                 │  setState(DOWNLOADING)                │               │
  │           Notify Ctrl: 0x04                             │               │
  │<─────────────────│                                │               │
  │                 │     │── GET firmware URL ─────────>│               │
  │                 │     │                    │─ R2.get(key) ───>│
  │                 │     │<── binary stream ───────────│<────────────│
  │                 │     │─ Update.write(buf,1024) loop │               │
  │         Notify Progress: [N%, 0x04]                     │               │
  │<──── x100 ─────│─────│                                │               │
  │                 │     │─ Update.end(true) MD5 verify  │               │
  │           Notify Ctrl: 0x05 (VERIFYING)                 │               │
  │           Notify Ctrl: 0x06 (READY)                     │               │
  │<─────────────────│                                │               │
  │                 │     │── POST /report ────────────>│               │
  │           Notify Ctrl: 0x07 (REBOOTING)                 │               │
  │<─────────────────│                                │               │
  │                 │     │─ ESP.restart()                │               │
  │     BLE 断开    │     │                                │               │
  │                 │                                      │               │
  │  ===== 重启后 =====                                     │               │
  │                 │     │─ otaConfirmIfNeeded()         │               │
  │                 │     │   mark_app_valid()            │               │
  │     BLE 重连    │     │                                │               │
  │─ Read OTA Info ─>│───>│                                │               │
  │<─ {cur:"1.2.0", new:""} │                             │               │
  │  更新完成 ✅     │     │                                │               │
```

### 11.5 Web 前端实现指南

#### BLE 连接与 OTA 服务发现

```javascript
// Web Bluetooth 连接
const device = await navigator.bluetooth.requestDevice({
  filters: [{ namePrefix: 'OAKIOT_' }],
  optionalServices: ['0000ff30-0000-1000-8000-00805f9b34fb']
});
const server = await device.gatt.connect();
const otaService = await server.getPrimaryService('0000ff30-0000-1000-8000-00805f9b34fb');

// 获取特征值
const charCtrl = await otaService.getCharacteristic('0000ff31-0000-1000-8000-00805f9b34fb');
const charInfo = await otaService.getCharacteristic('0000ff32-0000-1000-8000-00805f9b34fb');
const charProgress = await otaService.getCharacteristic('0000ff33-0000-1000-8000-00805f9b34fb');

// 订阅通知
await charCtrl.startNotifications();
await charProgress.startNotifications();
charCtrl.addEventListener('characteristicvaluechanged', onStateChange);
charProgress.addEventListener('characteristicvaluechanged', onProgress);
```

#### 检查更新流程

```javascript
// 发送检查命令
await charCtrl.writeValue(new Uint8Array([0x01]));

// 等待 Notify 状态变为 0x02 (AVAILABLE) 或 0x03 (NO_UPDATE)
function onStateChange(event) {
  const state = new Uint8Array(event.target.value.buffer)[0];
  if (state === 0x02) {
    // 有更新，读取详情
    readUpdateInfo();
  } else if (state === 0x03) {
    showMessage('已是最新版本');
  } else if (state >= 0xE0) {
    showError(stateToErrorMsg(state));
  }
}

async function readUpdateInfo() {
  const value = await charInfo.readValue();
  const json = new TextDecoder().decode(value.buffer);
  const info = JSON.parse(json);
  // info = { cur, new, size, log, force, state }
  showUpdateDialog(info);
}
```

#### 确认升级 + 进度监控

```javascript
// 用户点击“开始升级”
await charCtrl.writeValue(new Uint8Array([0x02]));

function onProgress(event) {
  const data = new Uint8Array(event.target.value.buffer);
  const percent = data[0];
  const state = data[1];
  updateProgressBar(percent);

  if (state === 0x06) showMessage('校验通过，即将重启');
  if (state === 0x07) showMessage('设备重启中，请等待重连...');
}
```

#### 取消操作

```javascript
// 仅在非下载状态时有效
await charCtrl.writeValue(new Uint8Array([0x03]));
```

### 11.6 Worker KV 数据结构总览

| Key 模式 | 值类型 | 说明 |
|---------|--------|------|
| `projects` | JSON 数组 | 所有项目 ID: `["oakiot", "clientA"]` |
| `products:{project}` | JSON 数组 | 某项目下的产品: `["moon-light", "sensor"]` |
| `hw_versions:{project}:{product}` | JSON 数组 | 某产品的硬件版本: `["esp32c3-v1"]` |
| `latest:{project}:{product}:{hw}` | JSON 对象 | 最新固件元数据（见 2.3 节） |

### 11.7 R2 路径规范

```
{project_id}/{product_id}/{hw_version}/firmware-{version}.bin
```

示例: `oakiot/moon-light/esp32c3-v1/firmware-1.2.0.bin`
