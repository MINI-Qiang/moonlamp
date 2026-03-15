# ESP32 WS2812 BLE 灯光控制协议

## 硬件配置

| 参数       | 值              |
| ---------- | --------------- |
| LED 类型   | WS2812B         |
| 数据引脚   | GPIO8           |
| LED 数量   | 15 颗（线性环状）|
| 色彩顺序   | GRB             |
| 颜色模式   | HSV / 动态灯效  |

## BLE 广播

| 参数     | 值                                        |
| -------- | ----------------------------------------- |
| 广播名称 | `OAKIO_XXXXXX`（XXXXXX 为 MAC 地址后三字节） |
| 扫描响应 | 开启                                      |

## BLE 服务

**LED 服务 UUID:** `0000ff00-0000-1000-8000-00805f9b34fb`

### 特征值一览

| 特征值   | UUID                                           | 属性              | 长度   | 说明       |
| -------- | ---------------------------------------------- | ----------------- | ------ | ---------- |
| HSV      | `0000ff01-0000-1000-8000-00805f9b34fb`         | Read / Write / Notify | 3 字节 | 颜色控制   |
| 开关     | `0000ff02-0000-1000-8000-00805f9b34fb`         | Read / Write / Notify | 1 字节 | 电源开关   |
| 灯效模式 | `0000ff03-0000-1000-8000-00805f9b34fb`         | Read / Write / Notify | 1 字节 | 灯效选择   |
| 灯效参数 | `0000ff04-0000-1000-8000-00805f9b34fb`         | Read / Write / Notify | 3 字节 | 灯效参数   |
| 时间灯效配置 | `0000ff05-0000-1000-8000-00805f9b34fb`     | Read / Write / Notify | 18 字节 | 时间智能灯配置 |

---

### HSV 特征值（0000ff01）

用于控制灯带颜色，数据格式为 3 字节：

| 字节偏移 | 字段 | 范围    | 说明                         |
| -------- | ---- | ------- | ---------------------------- |
| 0        | H    | 0 - 255 | 色相（Hue），映射 0°- 360°    |
| 1        | S    | 0 - 255 | 饱和度（Saturation）          |
| 2        | V    | 0 - 255 | 明度 / 亮度（Value）          |

**写入示例：**

- `[0x00, 0xFF, 0x80]` → 红色，全饱和，50% 亮度
- `[0x60, 0xFF, 0xFF]` → 绿色，全饱和，全亮度
- `[0xA0, 0xFF, 0xFF]` → 蓝色，全饱和，全亮度
- `[0x00, 0x00, 0xFF]` → 白色（饱和度为 0）

**默认值：** H=0, S=255, V=128

写入后设备会自动：
1. 更新灯带显示
2. 保存至 NVS 持久存储
3. 通过 Notify 回传当前值

---

### 开关特征值（0000ff02）

用于控制灯带开关，数据格式为 1 字节：

| 值   | 说明           |
| ---- | -------------- |
| 0x00 | 关闭（熄灭）   |
| 0x01 | 开启           |

**默认值：** 开启（0x01）

写入后设备会自动：
1. 更新灯带显示（关闭时所有 LED 熄灭，开启时恢复 HSV 颜色）
2. 保存至 NVS 持久存储
3. 通过 Notify 回传当前值

---

## 持久化存储

使用 ESP32 NVS（Preferences）存储，命名空间为 `led_cfg`：

| 键名      | 类型   | 说明         |
| --------- | ------ | ------------ |
| `hue`     | UChar  | 色相值       |
| `sat`     | UChar  | 饱和度       |
| `val`     | UChar  | 明度         |
| `power`   | Bool   | 开关状态     |
| `fxMode`  | UChar  | 灯效模式     |
| `fxSpeed` | UChar  | 灯效速度     |
| `fxP1`    | UChar  | 灯效参数1    |
| `fxP2`    | UChar  | 灯效参数2    |

设备上电时自动加载上次保存的设置。

## 连接行为

- 客户端断开连接后，设备会在 500ms 后自动重新开始 BLE 广播
- 支持单客户端连接

---

## 灯效模式特征值（0000ff03）

用于选择灯光效果模式，数据格式为 1 字节：

| 值   | 名称       | 说明                                       |
| ---- | ---------- | ------------------------------------------ |
| 0x00 | 静态HSV    | 使用 HSV 特征值设定的固定颜色（默认模式）     |
| 0x01 | 时间智能灯  | 基于日出日落的智能灯光控制（详见下文）       |
| 0x64 | 柔和呼吸   | (100) 平滑正弦呼吸灯，亮度缓慢起伏          |
| 0x65 | 色相漫游   | (101) Perlin噪声驱动的有机色彩漫游           |
| 0x66 | 调色板呼吸 | (102) 预设调色板色彩流转 + 呼吸亮度           |
| 0x67 | 烛光摇曳   | (103) 模拟烛火/白炽灯的暖色抖动              |
| 0x68 | 潮汐渐变   | (104) 双速正弦叠加的色相与亮度波动            |
| 0x69 | 日落渐变   | (105) 暖色调色板（深红→金黄→玫红）慢速漫游   |
| 0x6A | 极光流转   | (106) 多层正弦调制的极光色彩演变              |
| 0x6B | 心跳脉冲   | (107) 模拟心跳的双峰脉冲亮度变化              |
| 0x6C | 月光涟漪   | (108) 噪声色相 + 立方波亮度的柔和月光         |
| 0x6D | 色彩交融   | (109) 两种调色板之间的平滑过渡交融            |
| 0x6E | 萤火虫     | (110) 随机亮灭的暖色萤火虫闪烁               |
| 0x6F | 四季流转   | (111) 春夏秋冬四种调色板自动轮换              |

**默认值：** 0x00（静态 HSV）

写入后设备会自动：
1. 切换灯光效果（动态效果立即开始运行）
2. 保存至 NVS 持久存储
3. 通过 Notify 回传当前值

**注意：** 设置为动态灯效（1 或 100+）时，HSV 特征值的写入不影响灯效显示；切回 0 时恢复 HSV 静态颜色。

---

## 时间智能灯效（Mode = 0x01）

基于日出日落时间的智能灯光控制系统，自动调节亮度，适合作为日落灯/夜灯使用。

### 工作时间线

```
日出(~06:00)              日落(~18:00)  最亮(21:00)  夜灯(21:30)  关闭(~05:30)
     ↓                        ↓            ↓           ↓           ↓
[关闭]─────────────────────[渐亮]────────[最亮]────[渐暗]─[夜灯]─[关闭]
```

### 运行阶段

| 阶段 | 时间范围                 | 亮度行为                           |
| ---- | ------------------------ | ---------------------------------- |
| 0    | 日出 → 日落              | 白天关闭（亮度=0）                  |
| 1    | 日落 → 最亮时间          | 渐亮（亮度从0渐变到最大）           |
| 2    | 最亮时间 → 夜灯时间      | 最亮（维持最大亮度）                |
| 3    | 夜灯时间 → +渐暗时长     | 渐暗（亮度从最大渐变到夜灯亮度）    |
| 4    | 渐暗结束 → 关闭时间      | 夜灯（维持夜灯亮度）                |
| 5    | 关闭时间 → 日出          | 夜间关闭（亮度=0）                  |

### 日出日落计算

- **默认算法**：基于 NOAA 简化算法，精度约 ±5 分钟
- **默认坐标**：北京（纬度 39.9°，经度 116.4°）
- **时区**：UTC+8（北京时间）
- **扩展**：可通过 API 设置自定义坐标以支持其他地区

### 时间灯效配置参数

存储于 NVS 命名空间 `time_fx`：

| 参数             | 键名     | 类型   | 默认值 | 说明                                    |
| ---------------- | -------- | ------ | ------ | --------------------------------------- |
| 色相             | hue      | UChar  | 206    | HSV 色相（206 ≈ 291°，冷白光）           |
| 饱和度           | sat      | UChar  | 0      | HSV 饱和度（0 = 纯白）                   |
| 最大亮度         | maxV     | UChar  | 255    | 峰值亮度                                 |
| 夜灯亮度         | nightV   | UChar  | 30     | 夜灯模式亮度                             |
| 开始时间         | startT   | Short  | -1     | 渐亮开始（分钟数，-1=使用日落时间）      |
| 最亮时间         | peakT    | Short  | 1260   | 达到最亮的时间（21:00 = 1260 分钟）      |
| 夜灯时间         | nightT   | Short  | 1290   | 开始渐暗的时间（21:30 = 1290 分钟）      |
| 关闭时间         | offT     | Short  | -1     | 完全关闭（-1=日出前30分钟）              |
| 渐亮时长         | fadeUp   | UChar  | 0      | 渐亮时长（0=自动计算：日落到最亮时间）   |
| 渐暗时长         | fadeDown | UChar  | 30     | 渐暗时长（分钟）                         |

**时间参数格式：** 当天分钟数（0-1439），例如 21:00 = 21×60 = 1260

**特殊值 -1：** 使用日出日落计算结果

### 测试配置

默认使用以下 HSV 值：
- H: 206（映射到 291°）
- S: 0（纯白色）
- V: 255（最大亮度）

---

## 时间灯效配置特征值（0000ff05）

用于配置时间智能灯效参数，数据格式为 18 字节：

### 写入格式（14 字节）

| 字节偏移 | 字段           | 类型   | 默认值 | 说明                                |
| -------- | -------------- | ------ | ------ | ----------------------------------- |
| 0        | hue            | uint8  | 206    | 色相 0-255                           |
| 1        | saturation     | uint8  | 0      | 饱和度 0-255                         |
| 2        | maxBrightness  | uint8  | 255    | 最大亮度 0-255                       |
| 3        | nightBrightness| uint8  | 30     | 夜灯亮度 0-255                       |
| 4-5      | startTime      | int16  | -1     | 渐亮开始时间（分钟，-1=日落）        |
| 6-7      | peakTime       | int16  | 1260   | 最亮时间（分钟，21:00=1260）         |
| 8-9      | nightTime      | int16  | 1290   | 夜灯时间（分钟，21:30=1290）         |
| 10-11    | offTime        | int16  | -1     | 关闭时间（分钟，-1=日出前30分）      |
| 12       | fadeUpDuration | uint8  | 0      | 渐亮时长（分钟，0=自动）             |
| 13       | fadeDownDuration| uint8 | 30     | 渐暗时长（分钟）                     |

### 读取格式（18 字节）

读取时返回配置 + 状态信息：

| 字节偏移 | 字段       | 类型   | 说明                              |
| -------- | ---------- | ------ | --------------------------------- |
| 0-13     | config     | -      | 同上述 14 字节配置                 |
| 14       | phase      | uint8  | 当前阶段 (0-5)                     |
| 15       | brightness | uint8  | 当前实际亮度                       |
| 16       | sunriseH   | uint8  | 今日日出小时                       |
| 17       | sunsetH    | uint8  | 今日日落小时                       |

### 阶段枚举

| 值 | 阶段名称   |
| -- | ---------- |
| 0  | 白天关闭   |
| 1  | 渐亮中     |
| 2  | 最亮       |
| 3  | 渐暗中     |
| 4  | 夜灯       |
| 5  | 夜间关闭   |

**写入示例（JavaScript）：**
```javascript
const config = new Uint8Array(14);
config[0] = 206;   // hue
config[1] = 0;     // saturation
config[2] = 255;   // maxBrightness
config[3] = 30;    // nightBrightness
// startTime = -1 (跟随日落)
config[4] = 0xFF; config[5] = 0xFF;
// peakTime = 1260 (21:00)
config[6] = 1260 & 0xFF; config[7] = (1260 >> 8) & 0xFF;
// nightTime = 1290 (21:30)
config[8] = 1290 & 0xFF; config[9] = (1290 >> 8) & 0xFF;
// offTime = -1 (日出前30分)
config[10] = 0xFF; config[11] = 0xFF;
config[12] = 0;    // fadeUpDuration
config[13] = 30;   // fadeDownDuration
```

写入后设备会自动：
1. 应用新配置到时间灯效
2. 保存至 NVS 持久存储
3. 通过 Notify 回传当前配置和状态

---

## 灯效参数特征值（0000ff04）

用于调整当前灯效的参数，数据格式为 3 字节：

| 字节偏移 | 字段   | 范围    | 说明                   |
| -------- | ------ | ------- | ---------------------- |
| 0        | Speed  | 0 - 255 | 灯效速度（0慢 / 255快）|
| 1        | Param1 | 0 - 255 | 灯效参数1（含义因效果而异）|
| 2        | Param2 | 0 - 255 | 灯效参数2（含义因效果而异）|

**默认值：** Speed=128, Param1=128, Param2=128

### 各灯效参数说明

| 编号 | 灯效名称   | Speed 含义                         | Param1 含义                                   | Param2 含义                  |
| ---- | ---------- | ---------------------------------- | --------------------------------------------- | ---------------------------- |
| 100  | 柔和呼吸   | 呼吸频率（6-60 BPM）               | 最低亮度（0→0 / 255→200）                      | —                            |
| 101  | 色相漫游   | 漫游速度（0慢→255快）               | 饱和度（0-255直接映射）                         | —                            |
| 102  | 调色板呼吸 | 色彩流转速度                        | 调色板选择：0-42=彩虹, 43-85=海洋, 86-128=熔岩, 129-171=森林, 172-214=派对, 215-255=云彩 | 呼吸深度（0=浅 / 255=深）    |
| 103  | 烛光摇曳   | 摇曳频率（0慢→255快）               | 色温（0=暖黄 / 255=暖白）                       | 摇曳幅度（0=微 / 255=剧烈）  |
| 104  | 潮汐渐变   | 潮汐周期（0慢→255快）               | 色相波动范围（0=窄 / 255=宽）                   | 亮度波动幅度（0=小 / 255=大） |
| 105  | 日落渐变   | 流转速度（0慢→255快）               | —                                             | —                            |
| 106  | 极光流转   | 演变速度                            | 饱和度范围（0=宽 / 255=窄高饱和）               | 亮度深度（0=浅 / 255=深）    |
| 107  | 心跳脉冲   | 心率（40-120 BPM）                  | 峰值亮度（0→160 / 255→255）                     | —                            |
| 108  | 月光涟漪   | 变化速度（0慢→255快）               | 色相偏移基准（0-255）                           | —                            |
| 109  | 色彩交融   | 过渡速度（0慢→255快）               | 起始调色板（分段同102）                          | 目标调色板（分段同102）       |
| 110  | 萤火虫     | 闪烁频率（0→3秒/次 / 255→0.3秒/次）| 色彩范围（0=纯暖黄 / 255=暖黄→橙色28°偏移）    | —                            |
| 111  | 四季流转   | 季节流转速度（0→120秒/季 / 255→5秒/季）| —                                          | —                            |

**写入示例：**

- `[0x80, 0x80, 0x80]` → 默认参数（中等速度，中等参数）
- `[0xFF, 0x00, 0x00]` → 最快速度
- `[0x40, 0xC0, 0x80]` → 慢速，参数1=192，参数2=128

写入后设备会自动：
1. 立即应用新参数到当前运行的灯效
2. 保存至 NVS 持久存储
3. 通过 Notify 回传当前值

---

## WiFi 配网

设备支持**双模式配网**，根据 NVS 中是否已存储 WiFi 凭据自动选择启动模式：

### 模式切换

| 条件                        | 启动模式       | BLE 行为                       |
| ----------------------------- | -------------- | ------------------------------ |
| 默认启动                    | **正常模式**   | 自定义 GATT 服务（LED/WiFi/Time）|
| Web 端触发 WiFi 重置后重启 | **配网模式**   | WiFiProv Security1 BLE 配网     |

- 设备**始终默认进入正常模式**（广播 `OAKIOT_XXXXXX`），无论是否已存储 WiFi 凭据
- 无 WiFi 时仍可通过 BLE 控制灯光，确保设备始终可用
- 配网模式和正常模式**互斥**运行（ESP32 BLE 栈限制）
- 配网模式仅在 **Web 端主动触发 WiFi 重置**后进入（向 0xff11 写入 `0x00` → 设置 NVS 配网标志 → 重启）
- 配网成功后设备自动重启回到正常模式

#### 配网流程

```
用户使用中                                   Web 触发重置
     ↓                                           ↓
[正常模式 OAKIOT_] ----WiFi重置(0x00)--> [重启] --> [配网模式 PROV_OAKIOT_]
                                                                     ↓
     [正常模式 OAKIOT_] <----配网成功重启----          [安全配网中...]
```

---

### 方式一：安全配网 (WiFiProv Security1)

**由 Web 端触发 WiFi 重置后自动启用。** 基于 Espressif protocomm 协议，使用 X25519 + AES-256-CTR 加密。

#### BLE 广播

| 参数     | 值                                                |
| -------- | ------------------------------------------------- |
| 广播名称 | `PROV_OAKIO_XXXXXX`（MAC 后三字节）                 |
| PoP 验证码 | 基于设备 eFuse MAC 派生（每台设备唯一）     |
| 安全等级 | Security1（X25519 ECDH + PoP + AES-256-CTR）       |
| BLE 服务 UUID | `021a9004-0382-4aea-bff4-6b3f1c5adfb4`（128 位）   |

#### PoP 生成算法

```
pop = hex( SHA-256("OAKIO_POP_" + eFuse_MAC[6字节]) )[0:8]
```

取 SHA-256 哈希的前 4 字节转为 8 位十六进制字符串（如 `a3f1b20c`）。印刷在设备标签或通过串口输出。

#### Protocomm 端点

| 端点名称       | UUID     | 用途                  |
| -------------- | -------- | --------------------- |
| `prov-ctrl`    | `0xFF4F` | 控制端点               |
| `prov-scan`    | `0xFF50` | 扫描可用 WiFi 列表     |
| `prov-session` | `0xFF51` | 安全握手（X25519）     |
| `prov-config`  | `0xFF52` | 加密传输 SSID/密码     |
| `proto-ver`    | `0xFF53` | 协议版本（JSON）       |

#### Security1 握手流程

```
客户端                              ESP32 设备
  │                                   │
  │── Command0 (client_pubkey) ─────>│  X25519 公钥 (32B)
  │<── Response0 ───────────────────│  device_pubkey (32B) + device_random (16B)
  │                                   │
  │  双方: shared = ECDH(my_priv, peer_pub)
  │        shared[i] ^= SHA256(pop)[i]  // pop 为设备唯一 PoP 字符串
  │                                   │
  │── Command1 (encrypted verify) ──>│  AES-CTR(shared, IV, device_pubkey)
  │<── Response1 (encrypted verify) ─│  AES-CTR(shared, IV, client_pubkey)
  │                                   │
  │  === 安全会话建立，后续数据 AES-256-CTR 加密 ===
  │                                   │
  │── SetConfig (加密SSID+密码) ────>│
  │── ApplyConfig ──────────────────>│  设备尝试连接 WiFi
  │── GetStatus (轮询) ─────────────>│
  │<── Status: Connected ────────────│  成功 → 设备重启
```

#### AES-CTR 计数器管理

握手阶段 ESP-IDF 在同一个 AES-CTR 上下文 (`mbedtls_aes_crypt_ctr`) 中：
1. 解密 `client_verify` (32B) — 消耗计数器块 0-1
2. 加密 `device_verify` (32B) — 消耗计数器块 2-3

客户端验证 `device_verify` 时需将 `device_pubkey || device_verify` 拼接为 64B 一次性解密，使计数器对齐。

握手完成后共消耗 64 字节（4 个 AES 块），后续会话通信从字节偏移 64 开始。

**字节级偏移对齐：** ESP-IDF `mbedtls_aes_crypt_ctr` 通过 `nc_off` 追踪块内字节偏移，处理非 16 字节对齐数据时从块内中间位置继续。Web Crypto API 每次调用从整块边界开始，需通过前缀填充哑字节来对齐。

#### WiFi 扫描 (prov-scan)

安全会话建立后，可通过 `prov-scan` (0xFF50) 端点扫描附近 WiFi 网络：

```
客户端                              ESP32 设备
  │                                   │
  │── CmdScanStart (blocking=true) ─>│  触发 WiFi 扫描
  │<── RespScanStart ───────────────│
  │                                   │
  │── CmdScanStatus (轮询) ────────>│
  │<── RespScanStatus ──────────────│  scan_finished + result_count
  │                                   │
  │── CmdScanResult (0, 4) ────────>│  请求第 0~3 条
  │<── RespScanResult ──────────────│  WiFiScanResult[] (ssid, rssi, channel, auth)
  │── CmdScanResult (4, 4) ────────>│  请求第 4~7 条
  │<── ...                           │
```

每条 `WiFiScanResult` 包含 SSID (bytes)、信道 (uint32)、RSSI (int32)、BSSID (bytes)、认证模式 (WifiAuthMode 枚举)。

**WifiAuthMode 枚举：** 0=Open, 1=WEP, 2=WPA_PSK, 3=WPA2_PSK, 4=WPA_WPA2_PSK, 5=WPA2_Enterprise, 6=WPA3_PSK, 7=WPA2_WPA3_PSK

> ⚠️ BLE MTU 限制：每次 `CmdScanResult` 的 count 建议 ≤ 4，避免响应超出 MTU。

#### 兼容客户端

- Espressif 官方 App：[ESP BLE Provisioning](https://play.google.com/store/apps/details?id=com.espressif.provble) (Android/iOS)
- 本项目 Web 工具：`wifi-provision.html`（Web Bluetooth + Web Crypto）

---

### 方式二：明文配网 (自定义 GATT)

**正常模式下可用。** 通过自定义 BLE 特征值直接发送 WiFi 凭据。

**WiFi 服务 UUID:** `0000ff10-0000-1000-8000-00805f9b34fb`

### 特征值一览

| 特征值     | UUID                                           | 属性           | 说明           |
| ---------- | ---------------------------------------------- | -------------- | -------------- |
| WiFi配置   | `0000ff11-0000-1000-8000-00805f9b34fb`         | Read / Write   | SSID与密码设置 |
| WiFi状态   | `0000ff12-0000-1000-8000-00805f9b34fb`         | Read / Notify  | 连接状态       |

### WiFi 配置特征值（0000ff11）

**写入格式：** UTF-8 字符串 `SSID\nPASSWORD`（换行符分隔）

**写入示例：**
- `"MyWiFi\n12345678"` → SSID=MyWiFi, 密码=12345678
- `"OpenNetwork\n"` → SSID=OpenNetwork, 无密码（开放网络）

**特殊指令：**
- 写入单字节 `0x00` → 清除 WiFi 凭据并重启进入安全配网模式

**读取：** 返回当前已存储的 SSID（不含密码）

写入后设备自动保存凭据到 NVS 并尝试连接新网络。

### WiFi 状态特征值（0000ff12）

1 字节连接状态：

| 值   | 说明                           |
| ---- | ------------------------------ |
| 0x00 | 未连接（Disconnected）          |
| 0x01 | 正在连接（Connecting）          |
| 0x02 | 已连接（Connected）             |
| 0x03 | 连接失败（Failed）              |

状态变化时自动通过 Notify 推送。

### WiFi NVS 存储

命名空间 `wifi_cfg`：

| 键名   | 类型   | 说明       |
| ------ | ------ | ---------- |
| `ssid` | String | WiFi SSID  |
| `pass` | String | WiFi 密码  |

### WiFi 行为

- 上电自动加载已存储凭据并尝试连接
- 连接超时：15 秒
- 断开后自动重连间隔：30 秒
- WiFi 与 BLE 同时工作（ESP32 共存模式）

### WiFi 低功耗配置

设备默认启用最大省电模式以降低功耗：

| 参数               | 值               | 说明                                    |
| ------------------ | ---------------- | --------------------------------------- |
| 省电模式           | MAX_MODEM        | 最大调制解调器省电（WiFi 空闲时休眠）    |
| DTIM 监听间隔      | 10               | 约 1 秒唤醒一次检查数据                  |

**省电模式级别：**

| 模式值 | 名称        | 功耗   | 延迟     | 说明                              |
| ------ | ----------- | ------ | -------- | --------------------------------- |
| 0      | NONE        | 最高   | 最低     | 无省电，连续监听                   |
| 1      | MIN_MODEM   | 中等   | 中等     | 最小调制解调器省电，平衡模式       |
| 2      | MAX_MODEM   | 最低   | 较高     | 最大调制解调器省电（默认）         |

**API 函数：**
```cpp
setWiFiPowerMode(uint8_t mode);  // 设置省电模式 (0/1/2)
uint8_t getWiFiPowerMode();      // 获取当前省电模式
```

**注意：** MAX_MODEM 模式下，WiFi 响应延迟可能增加约 100-1000ms，但典型功耗可降低 50-70%。

---

## 时间服务

**时间服务 UUID:** `0000ff20-0000-1000-8000-00805f9b34fb`

### 特征值一览

| 特征值   | UUID                                           | 属性                  | 长度   | 说明       |
| -------- | ---------------------------------------------- | --------------------- | ------ | ---------- |
| 时间同步 | `0000ff21-0000-1000-8000-00805f9b34fb`         | Read / Write / Notify | 4 字节 | UTC epoch  |

### 时间同步特征值（0000ff21）

4 字节无符号整数（小端序 LE），表示 UTC epoch 秒数。

**读取：** 返回当前 UTC epoch

**写入：** 设置设备时间（UTC epoch）

**写入示例（JavaScript）：**
```javascript
const epoch = Math.floor(Date.now() / 1000);
const buf = new ArrayBuffer(4);
new DataView(buf).setUint32(0, epoch, true); // little-endian
```

### NTP 自动同步

- WiFi 连接成功后自动触发 NTP 同步
- 每 24 小时自动重新同步
- NTP 服务器：`pool.ntp.org`、`time.nist.gov`
- 时区：UTC+8（中国标准时间）

### BLE 授时（自定义）

无 WiFi 时可通过 BLE 写入 UTC epoch 手动授时。

---

## BLE 标准 Current Time Service (CTS)

**CTS 服务 UUID:** `0x1805`（Bluetooth SIG 标准）

同时提供标准 CTS 协议支持，兼容标准蓝牙时间客户端。

### 特征值一览

| 特征值         | UUID     | 属性                  | 长度   | 说明               |
| -------------- | -------- | --------------------- | ------ | ------------------ |
| Current Time   | `0x2A2B` | Read / Write / Notify | 10 字节 | 当前时间（标准格式）|
| Local Time Info| `0x2A0F` | Read                  | 2 字节  | 时区与夏令时        |

### Current Time 特征值（0x2A2B）

10 字节数据格式（小端序）：

| 字节偏移 | 字段          | 类型    | 说明                            |
| -------- | ------------- | ------- | ------------------------------- |
| 0-1      | Year          | uint16  | 年份（如 2026）                  |
| 2        | Month         | uint8   | 月（1-12）                       |
| 3        | Day           | uint8   | 日（1-31）                       |
| 4        | Hours         | uint8   | 时（0-23）                       |
| 5        | Minutes       | uint8   | 分（0-59）                       |
| 6        | Seconds       | uint8   | 秒（0-59）                       |
| 7        | Day of Week   | uint8   | 星期（1=周一...7=周日, 0=未知）  |
| 8        | Fractions256  | uint8   | 1/256 秒精度                     |
| 9        | Adjust Reason | uint8   | 调整原因位掩码                   |

**读取：** 返回设备当前本地时间

**写入：** 至少 7 字节可设置设备时间（字节 7-9 可选）

**写入示例（JavaScript）：**
```javascript
const now = new Date();
const buf = new Uint8Array(10);
buf[0] = now.getFullYear() & 0xFF;
buf[1] = (now.getFullYear() >> 8) & 0xFF;
buf[2] = now.getMonth() + 1;
buf[3] = now.getDate();
buf[4] = now.getHours();
buf[5] = now.getMinutes();
buf[6] = now.getSeconds();
buf[7] = now.getDay() === 0 ? 7 : now.getDay(); // CTS: 1=Mon..7=Sun
buf[8] = 0; // Fractions256
buf[9] = 0; // Adjust Reason
```

### Local Time Information 特征值（0x2A0F）

2 字节只读数据：

| 字节偏移 | 字段       | 类型  | 说明                                |
| -------- | ---------- | ----- | ----------------------------------- |
| 0        | Time Zone  | int8  | UTC 偏移，单位 15 分钟（UTC+8 = 32）|
| 1        | DST Offset | uint8 | 0=标准, 4=+1h, 255=未知             |

### CTS 与自定义时间服务的关系

- 通过 CTS 写入时间后，自定义时间同步特征值（0xff21）也会同步更新并 Notify
- 通过自定义特征值写入 epoch 后，CTS 读取也会返回更新后的时间
- NTP 同步后两个服务均自动更新
- 建议：标准客户端用 CTS，自定义 App 用 epoch 特征值（更简洁）

---

## 串口交互协议

设备通过 **USB 串口**（115200 baud, 8N1）提供 JSON 格式的命令交互接口，用于调试、工厂检测和工具集成。

### 传输层

| 参数     | 值                                      |
| -------- | --------------------------------------- |
| 波特率   | 115200                                  |
| 数据格式 | 8 数据位, 无校验, 1 停止位 (8N1)         |
| 编码     | UTF-8                                   |
| 帧分隔   | `\n`（换行符）或 `\r`（回车符）          |
| 最大长度 | 256 字节（单条命令）                     |

### 协议格式

#### 请求（主机 → 设备）

每条命令为**单行 JSON**，以换行符结尾：

```json
{"cmd": "<命令名>", ...参数}\n
```

| 字段  | 类型   | 必填 | 说明         |
| ----- | ------ | ---- | ------------ |
| `cmd` | string | 是   | 命令名称     |
| ...   | -      | 否   | 命令相关参数 |

#### 响应（设备 → 主机）

设备返回**单行 JSON**，以换行符结尾：

```json
{"resp": "<响应类型>", ...数据}\n
```

| 字段   | 类型   | 说明                         |
| ------ | ------ | ---------------------------- |
| `resp` | string | 响应类型（与命令对应或 `error`）|
| ...    | -      | 响应数据                     |

#### 错误响应

当命令无法识别时，返回通用错误响应：

```json
{"resp": "error", "msg": "unknown cmd: <命令名>"}
```

#### 非法输入处理

- 非法 JSON（解析失败）→ **静默忽略**，不返回任何内容
- 缺少 `cmd` 字段 → **静默忽略**
- 超过 256 字节的输入 → 超出部分被截断

---

### 命令列表

#### `get_device_info` — 获取设备信息

查询设备的标识信息和当前运行模式。

**请求：**

```json
{"cmd": "get_device_info"}
```

**响应：**

```json
{
  "resp": "device_info",
  "mac": "AA:BB:CC:DD:EE:FF",
  "pop": "A3F1B20C",
  "device_name": "OAKIOT_DDEEFF",
  "prov_name": "PROV_OAKIOT_DDEEFF",
  "provisioning": false
}
```

| 字段           | 类型    | 说明                                           |
| -------------- | ------- | ---------------------------------------------- |
| `mac`          | string  | 设备 eFuse MAC 地址（格式 `XX:XX:XX:XX:XX:XX`）|
| `pop`          | string  | 配网 PoP 验证码（8 位十六进制，SHA-256 派生）    |
| `device_name`  | string  | 正常模式 BLE 广播名称（`OAKIOT_XXXXXX`）        |
| `prov_name`    | string  | 配网模式 BLE 广播名称（`PROV_OAKIOT_XXXXXX`）   |
| `provisioning` | boolean | 当前是否处于配网模式                            |

---

#### `set_led` — 设置灯光颜色与亮度（生产测试）

用于生产线快速测试灯珠颜色和亮度，写入后**立即生效**，自动切换到静态 HSV 模式。该命令**不会**持久化到 NVS，设备重启后恢复原设置。

**请求：**

```json
{"cmd": "set_led", "h": 0, "s": 255, "v": 128, "on": true}
```

| 字段  | 类型    | 必填 | 默认值 | 范围    | 说明                          |
| ----- | ------- | ---- | ------ | ------- | ----------------------------- |
| `h`   | uint8   | 否   | 0      | 0-255   | 色相（Hue），映射 0°-360°      |
| `s`   | uint8   | 否   | 255    | 0-255   | 饱和度（Saturation）           |
| `v`   | uint8   | 否   | 128    | 0-255   | 明度/亮度（Value）             |
| `on`  | boolean | 否   | true   | -       | 开关（false=熄灭）             |

**响应：**

```json
{"resp": "led_ok", "h": 0, "s": 255, "v": 128, "on": true}
```

| 字段  | 类型    | 说明                 |
| ----- | ------- | -------------------- |
| `h`   | uint8   | 实际应用的色相       |
| `s`   | uint8   | 实际应用的饱和度     |
| `v`   | uint8   | 实际应用的亮度       |
| `on`  | boolean | 实际开关状态         |

**行为：**
1. 强制切换到静态 HSV 模式（`effectMode = 0`）
2. 立即更新所有 LED 显示
3. **不保存到 NVS**（重启恢复原值，避免干扰用户设置）

**生产线测试示例：**

```
# 全红 - 测试红色通道
{"cmd":"set_led","h":0,"s":255,"v":255}

# 全绿 - 测试绿色通道
{"cmd":"set_led","h":96,"s":255,"v":255}

# 全蓝 - 测试蓝色通道
{"cmd":"set_led","h":160,"s":255,"v":255}

# 纯白满亮 - 测试最大亮度
{"cmd":"set_led","h":0,"s":0,"v":255}

# 熄灭
{"cmd":"set_led","on":false}
```

---

### 串口日志输出

除 JSON 命令响应外，设备还通过串口输出带标签的日志信息，用于调试和状态监控：

| 标签     | 示例                                     | 说明               |
| -------- | ---------------------------------------- | ------------------ |
| `[SYS]`  | `[SYS] 正常模式 (WiFi已配置)`              | 系统启动与模式切换  |
| `[LED]`  | `[LED] 引脚: GPIO8, 数量: 15颗`           | LED 硬件配置       |
| `[BLE]`  | `[BLE] 重新开始广播`                       | BLE 连接与广播     |
| `[WiFi]` | `[WiFi] 已获取IP: 192.168.1.100`          | WiFi 连接状态      |
| `[Prov]` | `[Prov] PoP 验证码: A3F1B20C`             | 配网流程事件       |

**注意：** 日志输出为可读文本（非 JSON），仅供调试参考，不应作为机器解析依据。JSON 命令响应与日志互不干扰，唯一区分方式为：JSON 响应以 `{` 开头。

---

### 使用示例

#### Arduino 串口监视器

```
>>> {"cmd":"get_device_info"}
<<< {"resp":"device_info","mac":"AA:BB:CC:DD:EE:FF","pop":"A3F1B20C","device_name":"OAKIOT_DDEEFF","prov_name":"PROV_OAKIOT_DDEEFF","provisioning":false}
```

#### Python 脚本

```python
import serial, json

ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=2)
ser.write(b'{"cmd":"get_device_info"}\n')
line = ser.readline().decode('utf-8').strip()
info = json.loads(line)
print(f"MAC: {info['mac']}, PoP: {info['pop']}")
```

#### 典型启动日志

```
========================================
ESP32 WS2812 BLE灯光控制 + WiFi/NTP
========================================
[LED] 引脚: GPIO8, 数量: 15颗
[CFG] 运行时配置已加载
[SYS] 正常模式 (WiFi已配置)
[SYS] 系统就绪，等待BLE连接...
```

---

## OTA 远程固件更新

### OTA 服务 UUID

**OTA 服务 UUID:** `0000ff30-0000-1000-8000-00805f9b34fb`

### OTA BLE 特征值一览

| 特征值       | UUID                                           | 属性           | 长度   | 说明               |
| ------------ | ---------------------------------------------- | -------------- | ------ | ------------------ |
| OTA Control  | `0000ff31-0000-1000-8000-00805f9b34fb`         | Write / Notify | 1 字节 | 控制命令 & 状态通知 |
| OTA Info     | `0000ff32-0000-1000-8000-00805f9b34fb`         | Read / Notify  | 变长 JSON | 版本信息 + 更新详情 |
| OTA Progress | `0000ff33-0000-1000-8000-00805f9b34fb`         | Read / Notify  | 2 字节 | [进度%, 状态码]     |

### OTA 状态码

| 状态码 | 名称           | 说明                   |
| ------ | -------------- | ---------------------- |
| 0x00   | IDLE           | 空闲                   |
| 0x01   | CHECKING       | 正在检查更新           |
| 0x02   | AVAILABLE      | 有新版本可用           |
| 0x03   | NO_UPDATE      | 已是最新版本           |
| 0x04   | DOWNLOADING    | 正在下载固件           |
| 0x05   | VERIFYING      | 正在校验               |
| 0x06   | READY          | 校验通过，准备重启     |
| 0x07   | REBOOTING      | 即将重启               |
| 0x08   | SUCCESS        | 更新成功（重启后）     |
| 0xE0   | ERR_HTTP       | HTTP 请求失败          |
| 0xE1   | ERR_PARSE      | 响应解析失败           |
| 0xE2   | ERR_DOWNLOAD   | 下载失败               |
| 0xE3   | ERR_MD5        | MD5 校验失败           |
| 0xE4   | ERR_FLASH      | 写入 Flash 失败        |
| 0xE5   | ERR_NO_WIFI    | WiFi 未连接            |

### OTA Control 写入命令

| 命令     | 值     | 说明                 |
| -------- | ------ | -------------------- |
| 检查更新 | `0x01` | 手动触发服务器检查   |
| 确认升级 | `0x02` | 用户确认开始下载     |
| 取消     | `0x03` | 取消当前操作         |

**通知序列（完整升级）:**
```
0x01 (CHECKING) → 0x02 (AVAILABLE) → 0x04 (DOWNLOADING) → 0x05 (VERIFYING) → 0x06 (READY) → 0x07 (REBOOTING)
```

### OTA Progress 通知

下载过程中每变化 1% Notify，固定 2 字节：

| 字节 | 说明           | 范围   |
| ---- | -------------- | ------ |
| [0]  | 下载进度百分比 | 0-100  |
| [1]  | 当前状态码     | 见状态码表 |

### OTA Info 读取响应

Read 或 Notify 时返回 UTF-8 JSON 字符串：

**有可用更新时:**
```json
{"cur":"1.0.0","new":"1.2.0","size":483200,"log":"修复BLE断连；优化灯效","force":false,"state":2}
```

**无可用更新时:**
```json
{"cur":"1.0.0","new":"","state":0}
```

| 字段    | 类型    | 说明                               |
| ------- | ------- | ---------------------------------- |
| `cur`   | string  | 当前固件版本                       |
| `new`   | string  | 可用新版本（无更新时为空串）       |
| `size`  | number  | 固件大小(字节)，仅有更新时存在     |
| `log`   | string  | 更新日志，仅有更新时存在           |
| `force` | boolean | 是否强制更新，仅有更新时存在       |
| `state` | number  | 当前 OTA 状态码                    |

### OTA 自动检查逻辑

| 阶段       | 条件                              | 行为                                           |
| ---------- | --------------------------------- | ---------------------------------------------- |
| 首次检查   | 启动后等待 OTA_FIRST_DELAY (60s)  | NVS 无待更新 → 检查；有待更新 → 恢复 AVAILABLE |
| 定期检查   | 距上次 ≥ OTA_CHECK_INTERVAL (24h) | 仅 IDLE 且 WiFi 已连接时触发                   |
| 跳过       | 正在下载/校验/重启中              | 直接 return                                    |

### OTA 启动回滚保护

新固件写入后首次启动，`otaConfirmIfNeeded()` 调用 `esp_ota_mark_app_valid_cancel_rollback()` 确认分区有效。若新固件崩溃无法走到确认，ESP32 bootloader 自动回滚到上一个有效分区。

### OTA BLE 完整交互序列

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
     │── Write OTA Ctrl: 0x02 ──────────>│  确认开始升级
     │<── Notify OTA Ctrl: 0x04 ─────────│  (DOWNLOADING)
     │<── Notify Progress: [1%, 0x04] ───│
     │         ... 每变化1%通知 ...        │
     │<── Notify Progress: [100%, 0x04] ─│
     │<── Notify OTA Ctrl: 0x05 ─────────│  (VERIFYING)
     │<── Notify OTA Ctrl: 0x07 ─────────│  (REBOOTING)
     │         ... BLE 断开，设备重启 ...   │
```

---

## OTA 串口命令

#### `ota_check` — 手动检查更新

**请求:**
```json
{"cmd":"ota_check"}
```

**响应:**
```json
{"resp":"ota_status","state":2,"cur":"1.0.0","new":"1.2.0","size":483200}
```

#### `ota_update` — 确认开始升级

**请求:**
```json
{"cmd":"ota_update"}
```

**响应:**
```json
{"resp":"ota_status","state":4,"progress":0}
```

#### `ota_status` — 查询 OTA 状态

**请求:**
```json
{"cmd":"ota_status"}
```

**响应（无更新）:**
```json
{"resp":"ota_status","state":0,"progress":0,"cur":"1.0.0"}
```

**响应（有可用更新）:**
```json
{"resp":"ota_status","state":2,"progress":0,"cur":"1.0.0","new":"1.2.0","size":483200,"changelog":"修复BLE断连"}
```

#### `ota_cancel` — 取消操作

**请求:**
```json
{"cmd":"ota_cancel"}
```

**响应:**
```json
{"resp":"ota_status","state":0}
```

---

## 系统配置串口命令

### 统一运行时配置

设备支持通过串口/HTTP 实时修改系统配置参数，修改后持久化到 NVS，重启后保持。

#### `get_config` — 获取运行时配置

**请求:**
```json
{"cmd":"get_config"}
```

**响应:**
```json
{
  "ntp_server1":"ntp.aliyun.com",
  "ntp_server2":"time.nist.gov",
  "ntp_gmt_offset":28800,
  "ntp_daylight_offset":0,
  "device_prefix":"OAKIOT",
  "save_check_interval":300000,
  "ota_base_url":"https://ota.iot.oakiot.cc",
  "ota_check_interval":86400000,
  "ota_first_delay":60000,
  "ota_project_id":"oakiot",
  "ota_product_id":"moon-light",
  "ota_fw_version":"1.0.1",
  "ota_hw_version":"esp32c3-v1",
  "http_enabled":false,
  "http_port":80
}
```

| 字段                   | 类型    | 可写 | 说明                           |
| ---------------------- | ------- | ---- | ------------------------------ |
| `ntp_server1`          | string  | 是   | NTP 服务器 1                   |
| `ntp_server2`          | string  | 是   | NTP 服务器 2                   |
| `ntp_gmt_offset`       | number  | 是   | UTC 偏移 (秒)，28800=UTC+8     |
| `ntp_daylight_offset`  | number  | 是   | 夏令时偏移 (秒)                |
| `device_prefix`        | string  | 是   | BLE 广播名前缀 (重启后生效)    |
| `save_check_interval`  | number  | 是   | NVS 定时保存间隔 (ms)          |
| `ota_base_url`         | string  | 是   | OTA 服务器地址 (重启后生效)    |
| `ota_check_interval`   | number  | 是   | OTA 自动检查间隔 (ms)          |
| `ota_first_delay`      | number  | 是   | OTA 首次检查延迟 (ms)          |
| `ota_project_id`       | string  | 否   | 项目标识 (编译时常量)          |
| `ota_product_id`       | string  | 否   | 产品标识 (编译时常量)          |
| `ota_fw_version`       | string  | 否   | 固件版本 (编译时常量)          |
| `ota_hw_version`       | string  | 否   | 硬件版本 (编译时常量)          |
| `http_enabled`         | boolean | 是   | HTTP 服务开关                  |
| `http_port`            | number  | 是   | HTTP 服务端口                  |

#### `set_config` — 修改运行时配置

仅需传入要修改的字段，其他字段保持不变。修改后自动持久化到 NVS。

**请求示例（修改 NTP 服务器和时区）:**
```json
{"cmd":"set_config","ntp_server1":"pool.ntp.org","ntp_gmt_offset":32400}
```

**响应:** 返回完整配置 JSON（同 get_config）。

**请求示例（开启 HTTP 服务）:**
```json
{"cmd":"set_config","http_enabled":true,"http_port":80}
```

> **注意:** 部分配置修改需要重启才能完全生效（如 device_prefix 影响 BLE 广播名，ota_base_url 影响 OTA 检查）。

### 全量状态查询

#### `get_status` — 获取设备综合状态

**请求:**
```json
{"cmd":"get_status"}
```

**响应:**
```json
{
  "resp":"status",
  "mac":"AA:BB:CC:DD:EE:FF",
  "fw_version":"1.0.1",
  "hw_version":"esp32c3-v1",
  "wifi_status":2,
  "wifi_ssid":"MyWiFi",
  "time_synced":true,
  "time":"Sun, Jan 17 2026 15:24:38",
  "free_heap":120000,
  "uptime_ms":3600000,
  "power":true,
  "effect_mode":0,
  "ota_state":0,
  "http_running":true
}
```

### LED 与灯效控制

#### `get_led` — 获取 LED 状态

**请求:**
```json
{"cmd":"get_led"}
```

**响应:**
```json
{"resp":"led_state","power":true,"h":0,"s":255,"v":128,"effect_mode":0,"effect_speed":128,"effect_param1":128,"effect_param2":128}
```

#### `set_effect` — 设置灯效模式/参数

**请求:**
```json
{"cmd":"set_effect","mode":100,"speed":200,"param1":128}
```

| 字段     | 类型  | 必填 | 说明               |
| -------- | ----- | ---- | ------------------ |
| `mode`   | uint8 | 否   | 灯效模式 (0,1,100-111) |
| `speed`  | uint8 | 否   | 灯效速度 0-255     |
| `param1` | uint8 | 否   | 灯效参数1 0-255    |
| `param2` | uint8 | 否   | 灯效参数2 0-255    |

**响应:**
```json
{"resp":"effect_ok","effect_mode":100,"effect_speed":200,"effect_param1":128,"effect_param2":128}
```

### WiFi 管理

#### `get_wifi` — 获取 WiFi 状态

**请求:**
```json
{"cmd":"get_wifi"}
```

**响应:**
```json
{"resp":"wifi_state","status":2,"ssid":"MyWiFi","connected":true,"power_mode":2}
```

#### `set_wifi` — 设置 WiFi 凭据

**请求:**
```json
{"cmd":"set_wifi","ssid":"MyWiFi","pass":"12345678"}
```

**响应:**
```json
{"resp":"wifi_ok","ssid":"MyWiFi"}
```

### 时间灯效配置

#### `get_timefx` — 获取时间灯效配置

**请求:**
```json
{"cmd":"get_timefx"}
```

**响应:**
```json
{
  "resp":"timefx_config",
  "hue":206,"saturation":0,"max_brightness":255,"night_brightness":30,
  "start_time":-1,"peak_time":1260,"night_time":1290,"off_time":-1,
  "fade_up":0,"fade_down":30,"phase":0,"phase_name":"白天关闭"
}
```

#### `set_timefx` — 修改时间灯效配置

仅需传入要修改的字段。

**请求:**
```json
{"cmd":"set_timefx","max_brightness":200,"night_brightness":50}
```

**响应:**
```json
{"resp":"timefx_ok"}
```

### HTTP 服务控制

#### `set_http` — 开启/关闭 HTTP 服务

**请求（开启）:**
```json
{"cmd":"set_http","enabled":true,"port":80}
```

**响应:**
```json
{"resp":"http_ok","enabled":true,"port":80,"running":true}
```

**请求（关闭）:**
```json
{"cmd":"set_http","enabled":false}
```

---

## HTTP 局域网 API

设备支持通过 HTTP RESTful API 在局域网内进行配置和控制，默认关闭，需通过串口或 BLE 主动开启。

### 启用方式

串口命令:
```json
{"cmd":"set_http","enabled":true,"port":80}
```

或通过 `set_config`:
```json
{"cmd":"set_config","http_enabled":true,"http_port":80}
```

### 通用说明

| 参数         | 说明                                    |
| ------------ | --------------------------------------- |
| 默认端口     | 80                                      |
| Content-Type | application/json                        |
| CORS         | 允许所有来源 (`Access-Control-Allow-Origin: *`) |
| 认证         | 无（仅限局域网使用）                    |

### 端点列表

| 方法 | 路径              | 说明               |
| ---- | ----------------- | ------------------ |
| GET  | /api/status       | 设备综合状态       |
| GET  | /api/config       | 运行时配置         |
| PUT  | /api/config       | 修改配置           |
| GET  | /api/led          | LED 灯光状态       |
| PUT  | /api/led          | 设置灯光           |
| GET  | /api/effect       | 灯效模式与参数     |
| PUT  | /api/effect       | 设置灯效           |
| GET  | /api/timefx       | 时间灯效配置       |
| PUT  | /api/timefx       | 设置时间灯效       |
| GET  | /api/wifi         | WiFi 连接状态      |
| GET  | /api/ota          | OTA 状态           |
| POST | /api/ota/check    | 手动检查更新       |
| POST | /api/ota/update   | 确认升级           |
| POST | /api/ota/cancel   | 取消更新           |

### GET /api/status

返回设备综合状态（MAC、版本、WiFi、时间、LED、堆内存等）。

**响应示例:**
```json
{
  "mac":"AA:BB:CC:DD:EE:FF",
  "fw_version":"1.0.1",
  "hw_version":"esp32c3-v1",
  "project_id":"oakiot",
  "product_id":"moon-light",
  "wifi_status":2,
  "wifi_ssid":"MyWiFi",
  "time_synced":true,
  "time":"Sun, Mar 15 2026 15:24:38",
  "free_heap":120000,
  "uptime_ms":3600000,
  "power":true,
  "h":0,"s":255,"v":128,
  "effect_mode":0,
  "ota_state":0,
  "ota_progress":0
}
```

### GET /api/config

返回运行时配置（同串口 `get_config`）。

### PUT /api/config

修改运行时配置。请求体为 JSON，仅需包含要修改的字段。

**请求示例:**
```
PUT /api/config
Content-Type: application/json

{"ntp_server1":"pool.ntp.org","ntp_gmt_offset":32400}
```

**响应:** 返回完整配置 JSON。

### GET /api/led

**响应:**
```json
{"power":true,"h":0,"s":255,"v":128,"effect_mode":0,"effect_speed":128,"effect_param1":128,"effect_param2":128}
```

### PUT /api/led

设置灯光参数（支持部分字段）。

**请求:**
```json
{"power":true,"h":96,"s":255,"v":200}
```

### GET /api/effect

**响应:**
```json
{"effect_mode":100,"effect_speed":128,"effect_param1":128,"effect_param2":128}
```

### PUT /api/effect

**请求:**
```json
{"effect_mode":100,"effect_speed":200}
```

### GET /api/timefx

**响应:**
```json
{
  "hue":206,"saturation":0,"max_brightness":255,"night_brightness":30,
  "start_time":-1,"peak_time":1260,"night_time":1290,"off_time":-1,
  "fade_up":0,"fade_down":30,"phase":0,"phase_name":"白天关闭"
}
```

### PUT /api/timefx

**请求:**
```json
{"max_brightness":200,"night_brightness":50}
```

### GET /api/wifi

**响应:**
```json
{"status":2,"ssid":"MyWiFi","connected":true,"power_mode":2}
```

### GET /api/ota

**响应:**
```json
{"state":0,"progress":0,"cur":"1.0.1"}
```

有可用更新时:
```json
{"state":2,"progress":0,"cur":"1.0.1","new":"1.2.0","size":483200,"changelog":"修复BLE断连","force":false}
```

### POST /api/ota/check

手动触发检查更新。响应同 GET /api/ota。

### POST /api/ota/update

确认开始升级。响应同 GET /api/ota。

### POST /api/ota/cancel

取消更新。响应同 GET /api/ota。

### 智能家居集成示例

#### curl 命令行

```bash
# 查询设备状态
curl http://192.168.1.100/api/status

# 开灯设为暖白
curl -X PUT http://192.168.1.100/api/led \
  -H "Content-Type: application/json" \
  -d '{"power":true,"h":30,"s":180,"v":255}'

# 设置呼吸灯效
curl -X PUT http://192.168.1.100/api/effect \
  -H "Content-Type: application/json" \
  -d '{"effect_mode":100,"effect_speed":80}'

# 检查固件更新
curl -X POST http://192.168.1.100/api/ota/check
```

#### Python (Home Assistant 集成)

```python
import requests

DEVICE_IP = "192.168.1.100"

# 获取设备状态
status = requests.get(f"http://{DEVICE_IP}/api/status").json()
print(f"版本: {status['fw_version']}, 灯: {'开' if status['power'] else '关'}")

# 控制灯光
requests.put(f"http://{DEVICE_IP}/api/led",
             json={"power": True, "h": 0, "s": 255, "v": 200})

# 修改NTP服务器
requests.put(f"http://{DEVICE_IP}/api/config",
             json={"ntp_server1": "pool.ntp.org"})
```

---

## 配置渠道对比

| 配置项           | BLE         | 串口          | HTTP          |
| ---------------- | ----------- | ------------- | ------------- |
| LED HSV/开关     | 0xff01/02   | set_led       | PUT /api/led  |
| 灯效模式/参数    | 0xff03/04   | set_effect    | PUT /api/effect |
| 时间灯效         | 0xff05      | set_timefx    | PUT /api/timefx |
| WiFi 凭据        | 0xff11      | set_wifi      | —             |
| WiFi 状态        | 0xff12      | get_wifi      | GET /api/wifi |
| 时间同步         | 0xff21/2A2B | —             | —             |
| NTP/时区配置     | —           | set_config    | PUT /api/config |
| 设备前缀         | —           | set_config    | PUT /api/config |
| 存储间隔         | —           | set_config    | PUT /api/config |
| OTA URL/间隔     | —           | set_config    | PUT /api/config |
| OTA 检查/升级    | 0xff31      | ota_check等   | POST /api/ota/* |
| HTTP 服务开关    | —           | set_http      | PUT /api/config |
| 设备信息         | OTA Info    | get_device_info | GET /api/status |
```