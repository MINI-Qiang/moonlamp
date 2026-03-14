/**
 * Arduino 项目: ESP32_LED_TEST
 * 创建时间: 2026/1/17+
 * 
 * 功能: WS2812 LED灯带 BLE蓝牙控制 (HSV颜色模式 + 动态灯效)
 * 
 * 文件结构:
 *   config.h        - 引脚/UUID/常量定义 + 全局变量extern声明
 *   led_effects.h/cpp - 12种灯效实现 + 灯效调度 + applyLED
 *   ble_service.h/cpp - BLE初始化、回调、特征值同步
 *   storage.h/cpp     - NVS持久化读写
 *   wifi_service.h/cpp - WiFi连接管理、BLE配网
 *   time_service.h/cpp - NTP时间同步、BLE授时
 *   ota_service.h/cpp  - OTA远程固件更新
 *   ESP32_LED_TEST.ino - 全局变量定义 + setup/loop入口
 */

#include "config.h"
#include "led_effects.h"
#include "ble_service.h"
#include "storage.h"
#include "wifi_service.h"
#include "time_service.h"
#include "time_effect.h"
#include "sun_calc.h"
#include "ota_service.h"
#include <BLEDevice.h>
#include <WiFiProv.h>
#include <WiFi.h>
#include <Preferences.h>
#include "esp_mac.h"
#include "esp_wifi.h"
#include "mbedtls/sha256.h"
#include <ArduinoJson.h>

// ============ 全局变量定义 ============
CRGB leds[NUM_LEDS];

uint8_t currentH = 0;
uint8_t currentS = 255;
uint8_t currentV = 128;
bool    powerOn  = true;
uint8_t effectMode = 0;
uint8_t effectSpeed = 128;
uint8_t effectParam1 = 128;
uint8_t effectParam2 = 128;

BLECharacteristic *pCharHSV = nullptr;
BLECharacteristic *pCharPower = nullptr;
BLECharacteristic *pCharEffect = nullptr;
BLECharacteristic *pCharParam = nullptr;
BLECharacteristic *pCharTimeFx = nullptr;
BLECharacteristic *pCharWiFiCfg = nullptr;
BLECharacteristic *pCharWiFiStatus = nullptr;
BLECharacteristic *pCharTimeSync = nullptr;
BLECharacteristic *pCharCTSTime = nullptr;
BLECharacteristic *pCharCTSLocal = nullptr;
BLECharacteristic *pCharOtaCtrl = nullptr;
BLECharacteristic *pCharOtaInfo = nullptr;
BLECharacteristic *pCharOtaProgress = nullptr;
bool deviceConnected = false;
bool needRestart     = false;
bool provisioningMode = false;
volatile bool needApplyLED = false;

static unsigned long lastSaveCheckTime = 0;

// ============ OTA BLE 状态回调 ============
static void onOtaStateChange(uint8_t state, uint8_t progress) {
  if (!deviceConnected) return;
  // 通知 OTA Control 特征值（状态码）
  if (pCharOtaCtrl) {
    pCharOtaCtrl->setValue(&state, 1);
    pCharOtaCtrl->notify();
  }
  // 通知 OTA Progress 特征值 [progress%, state]
  if (pCharOtaProgress) {
    uint8_t buf[2] = { progress, state };
    pCharOtaProgress->setValue(buf, 2);
    pCharOtaProgress->notify();
  }
}

// ============ WiFiProv 事件回调 ============
static void provisioningEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_PROV_START:
      Serial.println("[Prov] 配网服务已启动");
      break;
    case ARDUINO_EVENT_PROV_CRED_RECV: {
      const char *ssid = (const char *)info.prov_cred_recv.ssid;
      const char *pass = (const char *)info.prov_cred_recv.password;
      Serial.printf("[Prov] 收到WiFi凭据: SSID=%s\n", ssid);
      // 桥接保存到 wifi_cfg 命名空间，使重启后 hasWiFiCredentials()=true
      // WiFiProv 将凭据存在 ESP-IDF 内部 NVS，与 wifi_cfg 不同步
      Preferences provPrefs;
      provPrefs.begin("wifi_cfg", false);
      provPrefs.putString("ssid", ssid);
      provPrefs.putString("pass", pass);
      provPrefs.end();
      break;
    }
    case ARDUINO_EVENT_PROV_CRED_FAIL:
      Serial.println("[Prov] WiFi凭据验证失败");
      {
        Preferences provPrefs;
        provPrefs.begin("wifi_cfg", false);
        provPrefs.remove("ssid");
        provPrefs.remove("pass");
        provPrefs.end();
      }
      break;
    case ARDUINO_EVENT_PROV_CRED_SUCCESS:
      Serial.println("[Prov] WiFi连接成功");
      break;
    case ARDUINO_EVENT_PROV_END:
      Serial.println("[Prov] 配网完成，即将重启进入正常模式...");
      delay(1000);
      ESP.restart();
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("[WiFi] 已获取IP: %s\n",
                    IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str());
      // 配网模式下，WiFiProv 可能复用 IDF 内部 NVS 旧凭据自动连接（不触发 PROV_CRED_RECV）
      // 必须将凭据桥接到 wifi_cfg，否则重启后又进入配网模式死循环
      if (provisioningMode) {
        Preferences bp;
        bp.begin("wifi_cfg", false);
        if (bp.getString("ssid", "").length() == 0) {
          wifi_config_t conf;
          if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK &&
              strlen((const char *)conf.sta.ssid) > 0) {
            bp.putString("ssid", (const char *)conf.sta.ssid);
            bp.putString("pass", (const char *)conf.sta.password);
            Serial.printf("[Prov] 已桥接WiFi凭据: SSID=%s\n", (const char *)conf.sta.ssid);
          }
        }
        bp.end();
      }
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("[WiFi] STA 已连接到AP");
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.printf("[WiFi] STA 断开 (reason=%d)\n", info.wifi_sta_disconnected.reason);
      break;
    case ARDUINO_EVENT_WIFI_STA_START:
      Serial.println("[WiFi] STA 启动");
      break;
  }
}

// ============ 生成配网BLE名称 ============
static String makeProvName() {
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  char name[24];
  snprintf(name, sizeof(name), "PROV_" DEVICE_PREFIX "_%02X%02X%02X", mac[3], mac[4], mac[5]);
  return String(name);
}

// ============ 生成设备唯一 PoP (SHA-256截断芯片ID) ============
static String makeDevicePoP() {
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);

  // 用 "OAKIO_POP" + MAC 做 SHA-256，确保与 MAC 本身不直接关联
  uint8_t input[16];
  memcpy(input, "OAKIO_POP_", 10);
  memcpy(input + 10, mac, 6);

  uint8_t hash[32];
  mbedtls_sha256(input, sizeof(input), hash, 0);

  // 取前4字节 = 8位十六进制
  char pop[PROV_POP_LEN + 1];
  for (int i = 0; i < PROV_POP_LEN / 2; i++) {
    snprintf(pop + i * 2, 3, "%02X", hash[i]);
  }
  return String(pop);
}

// ============ Setup ============
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n========================================");
  Serial.println("ESP32 WS2812 BLE灯光控制 + WiFi/NTP");
  Serial.println("========================================");
  Serial.printf("[LED] 引脚: GPIO%d, 数量: %d颗\n", LED_PIN, NUM_LEDS);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(255);

  initStorage();
  loadSettings();
  initTimeEffect();  // 初始化时间灯效系统
  applyLED();

  // ====== 检查配网标志: 仅在 Web 端主动触发重置后才进入 WiFiProv ======
  {
    Preferences sysCfg;
    sysCfg.begin("sys_cfg", false);
    bool needProv = sysCfg.getBool("needProv", false);
    if (needProv) {
      sysCfg.putBool("needProv", false);  // 清除标志，避免循环
      sysCfg.end();
      provisioningMode = true;
      Serial.println("[SYS] 安全配网模式 (WiFiProv Security1 BLE)");
      WiFi.onEvent(provisioningEvent);
      String provName = makeProvName();
      String provPoP = makeDevicePoP();
      Serial.printf("[Prov] 配网设备名: %s\n", provName.c_str());
      Serial.printf("[Prov] PoP 验证码: %s\n", provPoP.c_str());
      WiFiProv.beginProvision(
        PROV_TRANSPORT,
        NETWORK_PROV_SCHEME_HANDLER_NONE,
        NETWORK_PROV_SECURITY_1,
        provPoP.c_str(),
        provName.c_str()
      );
      Serial.println("[Prov] 等待手机/Web配网...");
      return;  // 配网模式不初始化自定义GATT
    }
    sysCfg.end();
  }

  // ====== 正常模式: 始终启动自定义 GATT 服务 ======
  provisioningMode = false;
  initWiFiService();   // 有凭据则连 WiFi，无凭据仅初始化等待BLE配网
  initTimeService();
  initOtaService();
  otaSetStateCallback(onOtaStateChange);
  otaConfirmIfNeeded();
  setupBLE();
  if (hasWiFiCredentials()) {
    Serial.println("[SYS] 正常模式 (WiFi已配置)");
  } else {
    Serial.println("[SYS] 正常模式 (WiFi未配置，等待BLE配网)");
  }
  Serial.println("[SYS] 系统就绪，等待BLE连接...");
}

// ============ 串口 JSON 命令处理 ============
static String serialBuf;

static void handleSerialCommand(const String &line) {
  JsonDocument req;
  if (deserializeJson(req, line)) return;  // 非法 JSON 直接忽略

  const char *action = req["cmd"];
  if (!action) return;

  JsonDocument resp;

  if (strcmp(action, "get_device_info") == 0) {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    char suffix[7];
    snprintf(suffix, sizeof(suffix), "%02X%02X%02X", mac[3], mac[4], mac[5]);

    resp["resp"] = "device_info";
    resp["mac"] = macStr;
    resp["pop"] = makeDevicePoP();
    resp["device_name"] = String(DEVICE_PREFIX "_") + suffix;
    resp["prov_name"] = String("PROV_" DEVICE_PREFIX "_") + suffix;
    resp["provisioning"] = provisioningMode;
    resp["project_id"] = OTA_PROJECT_ID;
    resp["product_id"] = OTA_PRODUCT_ID;
    resp["fw_version"] = OTA_FW_VERSION;
    resp["hw_version"] = OTA_HW_VERSION;
  } else if (strcmp(action, "set_led") == 0) {
    uint8_t h = req["h"] | 0;
    uint8_t s = req["s"] | 255;
    uint8_t v = req["v"] | 128;
    bool on  = req["on"] | true;

    currentH = h;
    currentS = s;
    currentV = v;
    powerOn  = on;
    effectMode = 0;  // 强制切到静态模式
    applyLED();

    resp["resp"] = "led_ok";
    resp["h"] = currentH;
    resp["s"] = currentS;
    resp["v"] = currentV;
    resp["on"] = powerOn;
  } else if (strcmp(action, "ota_check") == 0) {
    otaCheckNow();
    resp["resp"] = "ota_status";
    resp["state"] = getOtaState();
    resp["cur"] = OTA_FW_VERSION;
    const OtaUpdateInfo &info = getOtaUpdateInfo();
    if (info.available) {
      resp["new"] = info.version;
      resp["size"] = info.size;
    }
  } else if (strcmp(action, "ota_update") == 0) {
    otaStartUpdate();
    resp["resp"] = "ota_status";
    resp["state"] = getOtaState();
    resp["progress"] = getOtaProgress();
  } else if (strcmp(action, "ota_status") == 0) {
    resp["resp"] = "ota_status";
    resp["state"] = getOtaState();
    resp["progress"] = getOtaProgress();
    resp["cur"] = OTA_FW_VERSION;
    const OtaUpdateInfo &info = getOtaUpdateInfo();
    if (info.available) {
      resp["new"] = info.version;
      resp["size"] = info.size;
      resp["changelog"] = info.changelog;
    }
  } else if (strcmp(action, "ota_cancel") == 0) {
    otaCancelUpdate();
    resp["resp"] = "ota_status";
    resp["state"] = getOtaState();
  } else {
    resp["resp"] = "error";
    resp["msg"] = String("unknown cmd: ") + action;
  }

  serializeJson(resp, Serial);
  Serial.println();
}

static void pollSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBuf.length() > 0) {
        handleSerialCommand(serialBuf);
        serialBuf = "";
      }
    } else {
      if (serialBuf.length() < 256) serialBuf += c;
    }
  }
}

// ============ Loop ============
void loop() {
  pollSerial();

  // 安全配网模式: 仅运行灯效，等待WiFiProv回调
  if (provisioningMode) {
    if (effectMode == 1 || effectMode >= 100) updateEffect();
    static unsigned long provGotIpTime = 0;
    if (WiFi.status() == WL_CONNECTED) {
      if (provGotIpTime == 0) provGotIpTime = millis();
      if (millis() - provGotIpTime > 3000 && hasWiFiCredentials()) {
        Serial.println("[Prov] WiFi已就绪，重启进入正常模式...");
        delay(500);
        ESP.restart();
      }
    }
    FastLED.delay(1000 / FX_FPS);
    return;
  }

  // === 正常模式 ===
  if (needRestart) {
    delay(500);
    BLEDevice::startAdvertising();
    Serial.println("[BLE] 重新开始广播");
    needRestart = false;
  }

  if (needApplyLED) {
    needApplyLED = false;
    applyLED();
  }

  if (effectMode == 1 || effectMode >= 100) {
    updateEffect();
  }

  loopWiFiService();
  loopTimeService();
  loopOtaService(isWiFiConnected());
  syncWiFiTimeCharacteristics();

  unsigned long now = millis();
  if (now - lastSaveCheckTime >= SAVE_CHECK_INTERVAL) {
    lastSaveCheckTime = now;
    saveSettingsIfChanged();
  }

  FastLED.delay(1000 / FX_FPS);
}
