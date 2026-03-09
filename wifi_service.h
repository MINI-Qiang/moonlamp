#ifndef WIFI_SERVICE_H
#define WIFI_SERVICE_H

#include "config.h"

// WiFi连接状态
#define WIFI_ST_DISCONNECTED  0
#define WIFI_ST_CONNECTING    1
#define WIFI_ST_CONNECTED     2
#define WIFI_ST_FAILED        3

// 初始化WiFi服务（加载NVS凭据，尝试连接）
void initWiFiService();

// WiFi服务循环（状态监控、自动重连）
void loopWiFiService();

// 设置WiFi凭据（存入NVS并尝试连接）
bool setWiFiCredentials(const String &ssid, const String &password);

// 清除WiFi凭据
void clearWiFiCredentials();

// 获取WiFi连接状态
uint8_t getWiFiStatus();

// 获取已存储的SSID
String getStoredSSID();

// 是否已连接WiFi
bool isWiFiConnected();

// 是否已存储WiFi凭据
bool hasWiFiCredentials();

// 进入配网模式（清除凭据+重启）
void enterProvisioningMode();

// ============ 省电模式控制 ============
// mode: 0=无省电(NONE), 1=最小省电(MIN_MODEM), 2=最大省电(MAX_MODEM)
void setWiFiPowerMode(uint8_t mode);
uint8_t getWiFiPowerMode();

#endif
