#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <FastLED.h>

// ============ LED配置 ============
#define LED_PIN     8
#define NUM_LEDS    15
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
#define FX_FPS      60

// ============ BLE UUID ============
#define SERVICE_UUID        "0000ff00-0000-1000-8000-00805f9b34fb"
#define CHAR_HSV_UUID       "0000ff01-0000-1000-8000-00805f9b34fb"
#define CHAR_POWER_UUID     "0000ff02-0000-1000-8000-00805f9b34fb"
#define CHAR_EFFECT_UUID    "0000ff03-0000-1000-8000-00805f9b34fb"
#define CHAR_PARAM_UUID     "0000ff04-0000-1000-8000-00805f9b34fb"
#define CHAR_TIME_FX_UUID   "0000ff05-0000-1000-8000-00805f9b34fb"  // 时间灯效配置

// ============ WiFi配网服务 UUID ============
#define WIFI_SERVICE_UUID       "0000ff10-0000-1000-8000-00805f9b34fb"
#define CHAR_WIFI_CFG_UUID      "0000ff11-0000-1000-8000-00805f9b34fb"
#define CHAR_WIFI_STATUS_UUID   "0000ff12-0000-1000-8000-00805f9b34fb"

// ============ 自定义时间服务 UUID ============
#define TIME_SERVICE_UUID       "0000ff20-0000-1000-8000-00805f9b34fb"
#define CHAR_TIME_SYNC_UUID     "0000ff21-0000-1000-8000-00805f9b34fb"

// ============ BLE标准CTS (Current Time Service) ============
#define CTS_SERVICE_UUID        "1805"
#define CTS_CURRENT_TIME_UUID   "2A2B"
#define CTS_LOCAL_TIME_UUID     "2A0F"

// ============ NTP配置 ============
#define NTP_SERVER1         "ntp.aliyun.com"
#define NTP_SERVER2         "time.nist.gov"
#define NTP_GMT_OFFSET      28800       // UTC+8 (秒)
#define NTP_DAYLIGHT_OFFSET 0

// ============ WiFi配网服务配置 ============
#define DEVICE_PREFIX       "OAKIOT"      // 设备名称前缀 (广播名: OAKIOT_XXXXXX, 配网: PROV_OAKIOT_XXXXXX)
#define PROV_POP_LEN        8           // PoP长度 (8位十六进制)
#define PROV_TRANSPORT      NETWORK_PROV_SCHEME_BLE

// ============ 持久化 ============
#define SAVE_CHECK_INTERVAL 300000  // 定时检查间隔: 5分钟 (ms)

// ============ 全局状态 (extern声明) ============
extern CRGB leds[];

extern uint8_t currentH;
extern uint8_t currentS;
extern uint8_t currentV;
extern bool    powerOn;
extern uint8_t effectMode;
extern uint8_t effectSpeed;
extern uint8_t effectParam1;
extern uint8_t effectParam2;

extern bool deviceConnected;
extern bool needRestart;
extern bool provisioningMode;
extern volatile bool needApplyLED;

// BLE特征值指针
class BLECharacteristic;
extern BLECharacteristic *pCharHSV;
extern BLECharacteristic *pCharPower;
extern BLECharacteristic *pCharEffect;
extern BLECharacteristic *pCharParam;
extern BLECharacteristic *pCharTimeFx;  // 时间灯效配置

// WiFi/Time BLE特征值指针
extern BLECharacteristic *pCharWiFiCfg;
extern BLECharacteristic *pCharWiFiStatus;
extern BLECharacteristic *pCharTimeSync;

// CTS标准时间特征值指针
extern BLECharacteristic *pCharCTSTime;
extern BLECharacteristic *pCharCTSLocal;

#endif
