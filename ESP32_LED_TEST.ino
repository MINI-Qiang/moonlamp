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
#include "config_manager.h"
#include "http_service.h"
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

// ============ OTA 下载 LED 指示（重启后无 BLE 模式） ============
static void onOtaLedIndicator(uint8_t state, uint8_t progress) {
  switch (state) {
    case OTA_DOWNLOADING: {
      uint8_t b = map(progress, 0, 100, 30, 255);
      fill_solid(leds, NUM_LEDS, CHSV(128, 255, b));  // 青色，亮度随进度增加
      FastLED.show();
      break;
    }
    case OTA_VERIFYING: {
      // 紫色慢速脉冲
      uint8_t b = beatsin8(30, 80, 255);
      fill_solid(leds, NUM_LEDS, CHSV(200, 255, b));
      FastLED.show();
      break;
    }
    case OTA_READY:
    case OTA_REBOOTING:
      fill_solid(leds, NUM_LEDS, CHSV(96, 255, 255));   // 绿色常亮
      FastLED.show();
      break;
    default:
      if (state >= 0xE0) {
        // 红色三闪×2: 闪3下-暗-闪3下
        for (int g = 0; g < 2; g++) {
          for (int i = 0; i < 3; i++) {
            fill_solid(leds, NUM_LEDS, CHSV(0, 255, 255));
            FastLED.show(); delay(120);
            fill_solid(leds, NUM_LEDS, CRGB::Black);
            FastLED.show(); delay(120);
          }
          delay(300);
        }
      }
      break;
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

  initConfigManager();  // 统一配置管理器 (NVS加载运行时配置)
  initStorage();
  loadSettings();
  initTimeEffect();  // 初始化时间灯效系统
  applyLED();

  // ====== 检查 OTA 重启更新标志 ======
  if (otaHasPendingUpdate()) {
    Serial.println("[SYS] 检测到 OTA 更新标志，进入固件下载模式");
    otaSetStateCallback(onOtaLedIndicator);
    initWiFiService();
    // 等待 WiFi 连接（最长 30 秒），需调用 loopWiFiService 驱动状态机
    unsigned long wifiWait = millis();
    while (!isWiFiConnected() && millis() - wifiWait < 30000) {
      loopWiFiService();
      // 蓝色呼吸灯指示连接中
      uint8_t b = (millis() / 10) % 512;
      b = b > 255 ? 511 - b : b;
      fill_solid(leds, NUM_LEDS, CHSV(160, 255, b));
      FastLED.show();
      delay(10);
    }
    if (isWiFiConnected()) {
      otaRunPendingUpdate();
      // 下载失败：红色三闪×2 提示后继续正常启动
      for (int g = 0; g < 2; g++) {
        for (int i = 0; i < 3; i++) {
          fill_solid(leds, NUM_LEDS, CHSV(0, 255, 255));
          FastLED.show(); delay(120);
          fill_solid(leds, NUM_LEDS, CRGB::Black);
          FastLED.show(); delay(120);
        }
        delay(300);
      }
      Serial.println("[OTA] 下载失败，继续正常启动");
    } else {
      Serial.println("[OTA] WiFi 连接超时，继续正常启动");
      // 红色三闪×2
      for (int g = 0; g < 2; g++) {
        for (int i = 0; i < 3; i++) {
          fill_solid(leds, NUM_LEDS, CHSV(0, 255, 255));
          FastLED.show(); delay(120);
          fill_solid(leds, NUM_LEDS, CRGB::Black);
          FastLED.show(); delay(120);
        }
        delay(300);
      }
    }
    // 恢复正常 LED 状态
    applyLED();
  }

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
  initHttpService();  // HTTP 局域网配置服务 (根据 httpEnabled 决定是否启动)
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

  // ============ 配置管理命令 ============
  } else if (strcmp(action, "get_config") == 0) {
    // 直接输出 configToJson 并返回，避免二次序列化
    Serial.println(configToJson());
    return;

  } else if (strcmp(action, "set_config") == 0) {
    // 从请求 JSON 中提取配置字段（排除 cmd）
    String body;
    serializeJson(req, body);
    if (configFromJson(body)) {
      Serial.println(configToJson());
    } else {
      resp["resp"] = "error";
      resp["msg"] = "no valid config fields";
    }
    if (resp.containsKey("resp")) {
      serializeJson(resp, Serial);
      Serial.println();
    }
    return;

  // ============ LED 状态查询 ============
  } else if (strcmp(action, "get_led") == 0) {
    resp["resp"]         = "led_state";
    resp["power"]        = powerOn;
    resp["h"]            = currentH;
    resp["s"]            = currentS;
    resp["v"]            = currentV;
    resp["effect_mode"]  = effectMode;
    resp["effect_speed"] = effectSpeed;
    resp["effect_param1"]= effectParam1;
    resp["effect_param2"]= effectParam2;

  // ============ 灯效控制 ============
  } else if (strcmp(action, "set_effect") == 0) {
    if (req.containsKey("mode"))   effectMode   = req["mode"];
    if (req.containsKey("speed"))  effectSpeed  = req["speed"];
    if (req.containsKey("param1")) effectParam1 = req["param1"];
    if (req.containsKey("param2")) effectParam2 = req["param2"];
    needApplyLED = true;

    resp["resp"]         = "effect_ok";
    resp["effect_mode"]  = effectMode;
    resp["effect_speed"] = effectSpeed;
    resp["effect_param1"]= effectParam1;
    resp["effect_param2"]= effectParam2;

  // ============ WiFi 状态 ============
  } else if (strcmp(action, "get_wifi") == 0) {
    resp["resp"]       = "wifi_state";
    resp["status"]     = getWiFiStatus();
    resp["ssid"]       = getStoredSSID();
    resp["connected"]  = isWiFiConnected();
    resp["power_mode"] = getWiFiPowerMode();

  } else if (strcmp(action, "set_wifi") == 0) {
    const char* ssid = req["ssid"] | "";
    const char* pass = req["pass"] | "";
    if (strlen(ssid) > 0 && setWiFiCredentials(String(ssid), String(pass))) {
      resp["resp"] = "wifi_ok";
      resp["ssid"] = ssid;
    } else {
      resp["resp"] = "error";
      resp["msg"]  = "invalid ssid";
    }

  // ============ 时间灯效配置 ============
  } else if (strcmp(action, "get_timefx") == 0) {
    TimeEffectConfig& tfCfg = getTimeEffectConfig();
    resp["resp"]             = "timefx_config";
    resp["hue"]              = tfCfg.hue;
    resp["saturation"]       = tfCfg.saturation;
    resp["max_brightness"]   = tfCfg.maxBrightness;
    resp["night_brightness"] = tfCfg.nightBrightness;
    resp["start_time"]       = tfCfg.startTime;
    resp["peak_time"]        = tfCfg.peakTime;
    resp["night_time"]       = tfCfg.nightTime;
    resp["off_time"]         = tfCfg.offTime;
    resp["fade_up"]          = tfCfg.fadeUpDuration;
    resp["fade_down"]        = tfCfg.fadeDownDuration;
    resp["phase"]            = getTimeEffectPhase();
    resp["phase_name"]       = getTimeEffectPhaseName();

  } else if (strcmp(action, "set_timefx") == 0) {
    TimeEffectConfig& tfCfg = getTimeEffectConfig();
    if (req.containsKey("hue"))              tfCfg.hue = req["hue"];
    if (req.containsKey("saturation"))       tfCfg.saturation = req["saturation"];
    if (req.containsKey("max_brightness"))   tfCfg.maxBrightness = req["max_brightness"];
    if (req.containsKey("night_brightness")) tfCfg.nightBrightness = req["night_brightness"];
    if (req.containsKey("start_time"))       tfCfg.startTime = req["start_time"];
    if (req.containsKey("peak_time"))        tfCfg.peakTime = req["peak_time"];
    if (req.containsKey("night_time"))       tfCfg.nightTime = req["night_time"];
    if (req.containsKey("off_time"))         tfCfg.offTime = req["off_time"];
    if (req.containsKey("fade_up"))          tfCfg.fadeUpDuration = req["fade_up"];
    if (req.containsKey("fade_down"))        tfCfg.fadeDownDuration = req["fade_down"];
    saveTimeEffectConfig();
    resp["resp"] = "timefx_ok";

  // ============ HTTP 服务控制 ============
  } else if (strcmp(action, "set_http") == 0) {
    RuntimeConfig& rtCfg = getRuntimeConfig();
    if (req.containsKey("enabled")) rtCfg.httpEnabled = req["enabled"];
    if (req.containsKey("port"))    rtCfg.httpPort = req["port"];
    saveRuntimeConfig();
    resp["resp"]    = "http_ok";
    resp["enabled"] = rtCfg.httpEnabled;
    resp["port"]    = rtCfg.httpPort;
    resp["running"] = isHttpServiceRunning();

  // ============ 设备综合状态 ============
  } else if (strcmp(action, "get_status") == 0) {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    resp["resp"]         = "status";
    resp["mac"]          = macStr;
    resp["fw_version"]   = OTA_FW_VERSION;
    resp["hw_version"]   = OTA_HW_VERSION;
    resp["wifi_status"]  = getWiFiStatus();
    resp["wifi_ssid"]    = getStoredSSID();
    resp["time_synced"]  = isTimeSynced();
    resp["time"]         = getFormattedDateTime();
    resp["free_heap"]    = ESP.getFreeHeap();
    resp["uptime_ms"]    = millis();
    resp["power"]        = powerOn;
    resp["effect_mode"]  = effectMode;
    resp["ota_state"]    = getOtaState();
    resp["http_running"] = isHttpServiceRunning();

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
  loopHttpService();
  syncWiFiTimeCharacteristics();

  unsigned long now = millis();
  if (now - lastSaveCheckTime >= getRuntimeConfig().saveCheckInterval) {
    lastSaveCheckTime = now;
    saveSettingsIfChanged();
  }

  FastLED.delay(1000 / FX_FPS);
}
