#ifndef OTA_SERVICE_H
#define OTA_SERVICE_H

/**
 * OTA 远程固件更新服务
 * 
 * 功能：定期自动检查更新 → 通知用户 → 用户确认后下载升级
 * 依赖：WiFi 已连接、ArduinoJson、HTTPClient、Update
 * 
 * 设计为可跨项目复用的独立模块:
 *   - 通过 OTA_PROJECT_ID / OTA_PRODUCT_ID / OTA_HW_VERSION / OTA_FW_VERSION 宏配置
 *   - 通过 OTA_BASE_URL 指定服务器地址
 *   - 不依赖任何项目特定的头文件，仅需 Arduino 环境
 * 
 * 复用方法:
 *   1. 将 ota_service.h / ota_service.cpp 复制到新项目
 *   2. 在项目的 config.h 中定义 OTA_* 宏
 *   3. 在 setup() 中调用 initOtaService() + otaConfirmIfNeeded()
 *   4. 在 loop() 中调用 loopOtaService()
 */

#include <Arduino.h>

// ============ 编译检查：确保必要宏已定义 ============
#ifndef OTA_PROJECT_ID
  #error "OTA_PROJECT_ID must be defined (e.g., \"oakiot\")"
#endif
#ifndef OTA_PRODUCT_ID
  #error "OTA_PRODUCT_ID must be defined (e.g., \"led-light\")"
#endif
#ifndef OTA_HW_VERSION
  #error "OTA_HW_VERSION must be defined (e.g., \"esp32c3-v1\")"
#endif
#ifndef OTA_FW_VERSION
  #error "OTA_FW_VERSION must be defined (e.g., \"1.0.0\")"
#endif
#ifndef OTA_BASE_URL
  #error "OTA_BASE_URL must be defined (e.g., \"https://ota.iot.oakiot.cc\")"
#endif

// ============ 可选配置（可在 config.h 中覆盖默认值） ============
#ifndef OTA_CHECK_INTERVAL
  #define OTA_CHECK_INTERVAL  86400000UL  // 自动检查间隔: 24小时 (ms)
#endif
#ifndef OTA_FIRST_DELAY
  #define OTA_FIRST_DELAY     60000UL     // 首次检查延迟: 60秒 (ms)
#endif
#ifndef OTA_API_PATH
  #define OTA_API_PATH        "/api/v1/ota"
#endif

// ============ OTA 状态码 ============
#define OTA_IDLE          0x00  // 空闲
#define OTA_CHECKING      0x01  // 正在检查更新
#define OTA_AVAILABLE     0x02  // 有新版本可用
#define OTA_NO_UPDATE     0x03  // 已是最新版本
#define OTA_DOWNLOADING   0x04  // 正在下载固件
#define OTA_VERIFYING     0x05  // 正在校验
#define OTA_READY         0x06  // 校验通过，准备重启
#define OTA_REBOOTING     0x07  // 即将重启
#define OTA_SUCCESS       0x08  // 更新成功（重启后确认）
#define OTA_ERR_HTTP      0xE0  // HTTP 请求失败
#define OTA_ERR_PARSE     0xE1  // 响应解析失败
#define OTA_ERR_DOWNLOAD  0xE2  // 下载失败
#define OTA_ERR_MD5       0xE3  // MD5 校验失败
#define OTA_ERR_FLASH     0xE4  // 写入 Flash 失败
#define OTA_ERR_NO_WIFI   0xE5  // WiFi 未连接

// ============ 更新信息结构 ============
struct OtaUpdateInfo {
  bool     available;   // 是否有可用更新
  String   version;     // 新版本号
  String   url;         // 固件下载地址
  String   md5;         // 固件 MD5
  String   changelog;   // 更新日志
  uint32_t size;        // 固件大小（字节）
  bool     force;       // 是否强制更新
};

// ============ 状态变化回调（可选） ============
// 设置回调函数，当 OTA 状态变化时调用（如 BLE Notify）
// callback(state, progress) — state: OTA_* 状态码, progress: 0-100
typedef void (*OtaStateCallback)(uint8_t state, uint8_t progress);
void otaSetStateCallback(OtaStateCallback cb);

// ============ 核心接口 ============

// 初始化 OTA 服务（在 setup 中调用，WiFi 初始化之后）
void initOtaService();

// OTA 服务循环（在 loop 中调用，非阻塞）
// 需传入 WiFi 连接状态，解耦 WiFi 模块依赖
void loopOtaService(bool wifiConnected);

// 手动触发检查更新（BLE/Web/串口调用）
void otaCheckNow();

// 用户确认开始下载升级
void otaStartUpdate();

// 取消当前操作（回到 IDLE）
void otaCancelUpdate();

// 获取当前 OTA 状态
uint8_t getOtaState();

// 获取下载进度 (0-100)
uint8_t getOtaProgress();

// 获取当前固件版本号
const char* getOtaCurrentVersion();

// 获取可用更新信息（检查到更新后有效）
const OtaUpdateInfo& getOtaUpdateInfo();

// 启动后确认分区有效（防止回滚循环，在 setup 中调用）
void otaConfirmIfNeeded();

#endif // OTA_SERVICE_H
