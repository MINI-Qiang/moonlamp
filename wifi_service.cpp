#include "wifi_service.h"
#include <WiFi.h>
#include <Preferences.h>
#include "esp_wifi.h"

static Preferences wifiPrefs;
static uint8_t wifiState = WIFI_ST_DISCONNECTED;
static String storedSSID;
static String storedPass;
static unsigned long lastReconnect = 0;
static unsigned long connectStart = 0;

#define WIFI_RECONNECT_INTERVAL 30000  // 30秒重连间隔
#define WIFI_CONNECT_TIMEOUT    15000  // 15秒连接超时

// ============ 低功耗配置 ============
// WIFI_PS_NONE       - 无省电（最高功耗，最低延迟）
// WIFI_PS_MIN_MODEM  - 最小调制解调器省电（DTIM间隔唤醒，平衡模式）
// WIFI_PS_MAX_MODEM  - 最大调制解调器省电（最低功耗，延迟较高）
static wifi_ps_type_t currentPowerMode = WIFI_PS_MAX_MODEM;

// 低功耗模式下的监听间隔（越大越省电，但响应延迟增加）
// 范围: 1-16, 默认3, 每个单位约100ms
#define WIFI_LISTEN_INTERVAL  10

static void configPowerSave() {
  // 设置WiFi省电模式
  esp_wifi_set_ps(currentPowerMode);
  
  // 配置DTIM监听间隔
  wifi_config_t conf;
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK) {
    conf.sta.listen_interval = WIFI_LISTEN_INTERVAL;
    esp_wifi_set_config(WIFI_IF_STA, &conf);
  }
  
  const char* modeStr = (currentPowerMode == WIFI_PS_MAX_MODEM) ? "MAX_MODEM" :
                        (currentPowerMode == WIFI_PS_MIN_MODEM) ? "MIN_MODEM" : "NONE";
  Serial.printf("[WiFi] 省电模式: %s, 监听间隔: %d\n", modeStr, WIFI_LISTEN_INTERVAL);
}

static void startConnect() {
  if (storedSSID.length() == 0) return;
  wifiState = WIFI_ST_CONNECTING;
  connectStart = millis();
  WiFi.disconnect();
  WiFi.begin(storedSSID.c_str(), storedPass.c_str());
  Serial.printf("[WiFi] 正在连接: %s\n", storedSSID.c_str());
}

void initWiFiService() {
  wifiPrefs.begin("wifi_cfg", false);
  storedSSID = wifiPrefs.getString("ssid", "");
  storedPass = wifiPrefs.getString("pass", "");

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  
  // 启用低功耗配置
  configPowerSave();

  if (storedSSID.length() > 0) {
    Serial.printf("[WiFi] 已存储SSID: %s\n", storedSSID.c_str());
    startConnect();
  } else {
    Serial.println("[WiFi] 无WiFi凭据，等待BLE配网");
  }
}

void loopWiFiService() {
  if (storedSSID.length() == 0) return;

  unsigned long now = millis();
  wl_status_t status = WiFi.status();

  switch (wifiState) {
    case WIFI_ST_CONNECTING:
      if (status == WL_CONNECTED) {
        wifiState = WIFI_ST_CONNECTED;
        Serial.printf("[WiFi] 已连接! IP: %s\n", WiFi.localIP().toString().c_str());
        // 连接成功后重新应用省电配置
        configPowerSave();
      } else if (now - connectStart > WIFI_CONNECT_TIMEOUT) {
        wifiState = WIFI_ST_FAILED;
        lastReconnect = now;
        Serial.println("[WiFi] 连接超时");
      }
      break;

    case WIFI_ST_CONNECTED:
      if (status != WL_CONNECTED) {
        wifiState = WIFI_ST_DISCONNECTED;
        lastReconnect = now;
        Serial.println("[WiFi] 连接断开");
      }
      break;

    case WIFI_ST_DISCONNECTED:
    case WIFI_ST_FAILED:
      if (now - lastReconnect >= WIFI_RECONNECT_INTERVAL) {
        startConnect();
      }
      break;
  }
}

bool setWiFiCredentials(const String &ssid, const String &password) {
  if (ssid.length() == 0 || ssid.length() > 32) return false;

  storedSSID = ssid;
  storedPass = password;
  wifiPrefs.putString("ssid", ssid);
  wifiPrefs.putString("pass", password);
  Serial.printf("[WiFi] 凭据已保存: SSID=%s\n", ssid.c_str());

  startConnect();
  return true;
}

void clearWiFiCredentials() {
  storedSSID = "";
  storedPass = "";
  wifiPrefs.remove("ssid");
  wifiPrefs.remove("pass");
  // 同时清除 ESP-IDF 内部 NVS (nvs.net80211) 存储的 WiFi 配置
  // 否则 WiFiProv 重启后会复用旧凭据自动连接，绕过配网流程
  esp_wifi_restore();
  WiFi.disconnect(true, true);
  wifiState = WIFI_ST_DISCONNECTED;
  Serial.println("[WiFi] 凭据已清除(含ESP-IDF内部存储)");
}

uint8_t getWiFiStatus() {
  return wifiState;
}

String getStoredSSID() {
  return storedSSID;
}

bool isWiFiConnected() {
  return wifiState == WIFI_ST_CONNECTED;
}

bool hasWiFiCredentials() {
  Preferences p;
  p.begin("wifi_cfg", true);
  String ssid = p.getString("ssid", "");
  p.end();
  return ssid.length() > 0;
}

void enterProvisioningMode() {
  Serial.println("[WiFi] 进入配网模式，清除凭据并重启...");
  clearWiFiCredentials();
  delay(500);
  ESP.restart();
}

// ============ 省电模式控制 ============
void setWiFiPowerMode(uint8_t mode) {
  switch (mode) {
    case 0:
      currentPowerMode = WIFI_PS_NONE;
      break;
    case 1:
      currentPowerMode = WIFI_PS_MIN_MODEM;
      break;
    case 2:
    default:
      currentPowerMode = WIFI_PS_MAX_MODEM;
      break;
  }
  if (wifiState == WIFI_ST_CONNECTED) {
    configPowerSave();
  }
}

uint8_t getWiFiPowerMode() {
  if (currentPowerMode == WIFI_PS_NONE) return 0;
  if (currentPowerMode == WIFI_PS_MIN_MODEM) return 1;
  return 2;  // WIFI_PS_MAX_MODEM
}
