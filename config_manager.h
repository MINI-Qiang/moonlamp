#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

/**
 * 统一运行时配置管理器
 * 
 * 集中管理所有可通过串口/HTTP/BLE运行时修改的系统参数。
 * 使用 NVS 持久化，#define 宏值作为默认值。
 * 
 * 管理范围:
 *   - NTP 配置 (服务器、时区)
 *   - 设备标识 (名称前缀)
 *   - 存储间隔
 *   - OTA 运行时参数 (URL、检查间隔)
 *   - HTTP 服务开关
 * 
 * 不管理 (由各模块自行管理):
 *   - LED 状态 (storage.cpp)
 *   - WiFi 凭据 (wifi_service.cpp)
 *   - 时间灯效参数 (time_effect.cpp)
 *   - OTA 更新信息 (ota_service.cpp)
 */

#include <Arduino.h>

// ============ 运行时配置结构 ============
struct RuntimeConfig {
  // NTP
  char ntpServer1[64];
  char ntpServer2[64];
  long gmtOffset;           // UTC 偏移 (秒), 默认 28800 (UTC+8)
  int  daylightOffset;      // 夏令时偏移 (秒), 默认 0

  // 设备
  char devicePrefix[16];    // BLE 广播名前缀, 默认 "OAKIOT"

  // 存储
  unsigned long saveCheckInterval;  // 定时保存间隔 (ms), 默认 300000

  // OTA 运行时可调参数
  char otaBaseUrl[128];             // OTA 服务器地址
  unsigned long otaCheckInterval;   // 自动检查间隔 (ms)
  unsigned long otaFirstDelay;      // 首次检查延迟 (ms)

  // HTTP 服务
  bool     httpEnabled;     // 是否启用 HTTP 服务, 默认 false
  uint16_t httpPort;        // HTTP 端口, 默认 80
};

// ============ 接口 ============

// 初始化配置管理器 (从 NVS 加载，缺省用 #define 默认值)
void initConfigManager();

// 获取运行时配置引用
RuntimeConfig& getRuntimeConfig();

// 保存当前配置到 NVS
void saveRuntimeConfig();

// 序列化为 JSON 字符串 (包含所有可配置项 + 只读信息)
String configToJson();

// 从 JSON 更新配置 (仅更新 JSON 中存在的字段)
// 返回 true 表示有字段被修改
bool configFromJson(const String& json);

#endif
