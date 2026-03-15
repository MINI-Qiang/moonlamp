#include "http_service.h"
#include "config.h"
#include "config_manager.h"
#include "i18n.h"
#include "wifi_service.h"
#include "time_service.h"
#include "time_effect.h"
#include "ota_service.h"
#include "led_effects.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <esp_mac.h>

static WebServer* server = nullptr;
static bool running = false;

// ============ CORS 辅助 ============
static void sendCors() {
  server->sendHeader("Access-Control-Allow-Origin", "*");
  server->sendHeader("Access-Control-Allow-Methods", "GET, PUT, POST, OPTIONS");
  server->sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

static void handleOptions() {
  sendCors();
  server->send(204);
}

static void sendJson(int code, const String& json) {
  sendCors();
  server->send(code, "application/json", json);
}

// ============ GET /api/status ============
static void handleGetStatus() {
  JsonDocument doc;

  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  doc["mac"]          = macStr;
  doc["fw_version"]   = OTA_FW_VERSION;
  doc["hw_version"]   = OTA_HW_VERSION;
  doc["project_id"]   = OTA_PROJECT_ID;
  doc["product_id"]   = OTA_PRODUCT_ID;
  doc["wifi_status"]  = getWiFiStatus();
  doc["wifi_ssid"]    = getStoredSSID();
  doc["time_synced"]  = isTimeSynced();
  doc["time"]         = getFormattedDateTime();
  doc["free_heap"]    = ESP.getFreeHeap();
  doc["uptime_ms"]    = millis();

  // LED
  doc["power"]        = powerOn;
  doc["h"]            = currentH;
  doc["s"]            = currentS;
  doc["v"]            = currentV;
  doc["effect_mode"]  = effectMode;

  // OTA
  doc["ota_state"]    = getOtaState();
  doc["ota_progress"] = getOtaProgress();

  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

// ============ GET /api/config ============
static void handleGetConfig() {
  sendJson(200, configToJson());
}

// ============ PUT /api/config ============
static void handlePutConfig() {
  if (!server->hasArg("plain")) {
    sendJson(400, "{\"error\":\"missing body\"}");
    return;
  }
  String body = server->arg("plain");
  if (configFromJson(body)) {
    sendJson(200, configToJson());
  } else {
    sendJson(400, "{\"error\":\"invalid config\"}");
  }
}

// ============ GET /api/led ============
static void handleGetLed() {
  JsonDocument doc;
  doc["power"] = powerOn;
  doc["h"]     = currentH;
  doc["s"]     = currentS;
  doc["v"]     = currentV;
  doc["effect_mode"]   = effectMode;
  doc["effect_speed"]  = effectSpeed;
  doc["effect_param1"] = effectParam1;
  doc["effect_param2"] = effectParam2;

  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

// ============ PUT /api/led ============
static void handlePutLed() {
  if (!server->hasArg("plain")) {
    sendJson(400, "{\"error\":\"missing body\"}");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server->arg("plain"))) {
    sendJson(400, "{\"error\":\"invalid json\"}");
    return;
  }

  if (doc.containsKey("power")) powerOn = doc["power"];
  if (doc.containsKey("h"))     currentH = doc["h"];
  if (doc.containsKey("s"))     currentS = doc["s"];
  if (doc.containsKey("v"))     currentV = doc["v"];

  needApplyLED = true;

  JsonDocument resp;
  resp["resp"]  = "led_ok";
  resp["power"] = powerOn;
  resp["h"]     = currentH;
  resp["s"]     = currentS;
  resp["v"]     = currentV;
  String out;
  serializeJson(resp, out);
  sendJson(200, out);
}

// ============ GET /api/effect ============
static void handleGetEffect() {
  JsonDocument doc;
  doc["effect_mode"]   = effectMode;
  doc["effect_speed"]  = effectSpeed;
  doc["effect_param1"] = effectParam1;
  doc["effect_param2"] = effectParam2;

  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

// ============ PUT /api/effect ============
static void handlePutEffect() {
  if (!server->hasArg("plain")) {
    sendJson(400, "{\"error\":\"missing body\"}");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server->arg("plain"))) {
    sendJson(400, "{\"error\":\"invalid json\"}");
    return;
  }

  if (doc.containsKey("effect_mode"))   effectMode   = doc["effect_mode"];
  if (doc.containsKey("effect_speed"))  effectSpeed  = doc["effect_speed"];
  if (doc.containsKey("effect_param1")) effectParam1 = doc["effect_param1"];
  if (doc.containsKey("effect_param2")) effectParam2 = doc["effect_param2"];

  needApplyLED = true;

  // 回显
  handleGetEffect();
}

// ============ GET /api/timefx ============
static void handleGetTimeFx() {
  TimeEffectConfig& cfg = getTimeEffectConfig();
  JsonDocument doc;
  doc["hue"]             = cfg.hue;
  doc["saturation"]      = cfg.saturation;
  doc["max_brightness"]  = cfg.maxBrightness;
  doc["night_brightness"]= cfg.nightBrightness;
  doc["start_time"]      = cfg.startTime;
  doc["peak_time"]       = cfg.peakTime;
  doc["night_time"]      = cfg.nightTime;
  doc["off_time"]        = cfg.offTime;
  doc["fade_up"]         = cfg.fadeUpDuration;
  doc["fade_down"]       = cfg.fadeDownDuration;
  doc["phase"]           = getTimeEffectPhase();
  doc["phase_name"]      = getTimeEffectPhaseName();

  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

// ============ PUT /api/timefx ============
static void handlePutTimeFx() {
  if (!server->hasArg("plain")) {
    sendJson(400, "{\"error\":\"missing body\"}");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server->arg("plain"))) {
    sendJson(400, "{\"error\":\"invalid json\"}");
    return;
  }

  TimeEffectConfig& cfg = getTimeEffectConfig();
  if (doc.containsKey("hue"))              cfg.hue = doc["hue"];
  if (doc.containsKey("saturation"))       cfg.saturation = doc["saturation"];
  if (doc.containsKey("max_brightness"))   cfg.maxBrightness = doc["max_brightness"];
  if (doc.containsKey("night_brightness")) cfg.nightBrightness = doc["night_brightness"];
  if (doc.containsKey("start_time"))       cfg.startTime = doc["start_time"];
  if (doc.containsKey("peak_time"))        cfg.peakTime = doc["peak_time"];
  if (doc.containsKey("night_time"))       cfg.nightTime = doc["night_time"];
  if (doc.containsKey("off_time"))         cfg.offTime = doc["off_time"];
  if (doc.containsKey("fade_up"))          cfg.fadeUpDuration = doc["fade_up"];
  if (doc.containsKey("fade_down"))        cfg.fadeDownDuration = doc["fade_down"];

  saveTimeEffectConfig();
  handleGetTimeFx();
}

// ============ GET /api/wifi ============
static void handleGetWifi() {
  JsonDocument doc;
  doc["status"]     = getWiFiStatus();
  doc["ssid"]       = getStoredSSID();
  doc["connected"]  = isWiFiConnected();
  doc["power_mode"] = getWiFiPowerMode();

  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

// ============ GET /api/ota ============
static void handleGetOta() {
  JsonDocument doc;
  doc["state"]    = getOtaState();
  doc["progress"] = getOtaProgress();
  doc["cur"]      = getOtaCurrentVersion();
  const OtaUpdateInfo& info = getOtaUpdateInfo();
  if (info.available) {
    doc["new"]       = info.version;
    doc["size"]      = info.size;
    doc["changelog"] = info.changelog;
    doc["force"]     = info.force;
  }

  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

// ============ POST /api/ota/check ============
static void handleOtaCheck() {
  otaCheckNow();
  handleGetOta();
}

// ============ POST /api/ota/update ============
static void handleOtaUpdate() {
  otaStartUpdate();
  handleGetOta();
}

// ============ POST /api/ota/cancel ============
static void handleOtaCancel() {
  otaCancelUpdate();
  handleGetOta();
}

// ============ 生命周期 ============

void initHttpService() {
  RuntimeConfig& cfg = getRuntimeConfig();
  if (cfg.httpEnabled && isWiFiConnected()) {
    startHttpService();
  }
}

void startHttpService() {
  if (running) return;
  if (!isWiFiConnected()) {
    Serial.println(TR("[HTTP] WiFi not connected, cannot start", "[HTTP] WiFi 未连接，无法启动 HTTP 服务"));
    return;
  }

  RuntimeConfig& cfg = getRuntimeConfig();
  server = new WebServer(cfg.httpPort);

  // OPTIONS (CORS 预检)
  server->on("/api/status",     HTTP_OPTIONS, handleOptions);
  server->on("/api/config",     HTTP_OPTIONS, handleOptions);
  server->on("/api/led",        HTTP_OPTIONS, handleOptions);
  server->on("/api/effect",     HTTP_OPTIONS, handleOptions);
  server->on("/api/timefx",     HTTP_OPTIONS, handleOptions);
  server->on("/api/wifi",       HTTP_OPTIONS, handleOptions);
  server->on("/api/ota",        HTTP_OPTIONS, handleOptions);
  server->on("/api/ota/check",  HTTP_OPTIONS, handleOptions);
  server->on("/api/ota/update", HTTP_OPTIONS, handleOptions);
  server->on("/api/ota/cancel", HTTP_OPTIONS, handleOptions);

  // GET
  server->on("/api/status",     HTTP_GET,  handleGetStatus);
  server->on("/api/config",     HTTP_GET,  handleGetConfig);
  server->on("/api/led",        HTTP_GET,  handleGetLed);
  server->on("/api/effect",     HTTP_GET,  handleGetEffect);
  server->on("/api/timefx",     HTTP_GET,  handleGetTimeFx);
  server->on("/api/wifi",       HTTP_GET,  handleGetWifi);
  server->on("/api/ota",        HTTP_GET,  handleGetOta);

  // PUT
  server->on("/api/config",     HTTP_PUT,  handlePutConfig);
  server->on("/api/led",        HTTP_PUT,  handlePutLed);
  server->on("/api/effect",     HTTP_PUT,  handlePutEffect);
  server->on("/api/timefx",     HTTP_PUT,  handlePutTimeFx);

  // POST
  server->on("/api/ota/check",  HTTP_POST, handleOtaCheck);
  server->on("/api/ota/update", HTTP_POST, handleOtaUpdate);
  server->on("/api/ota/cancel", HTTP_POST, handleOtaCancel);

  // 404
  server->onNotFound([]() {
    sendCors();
    server->send(404, "application/json", "{\"error\":\"not found\"}");
  });

  server->begin();
  running = true;
  Serial.printf(TR("[HTTP] Service started (port %d, IP %s)\n",
                "[HTTP] 服务已启动 (端口 %d, IP %s)\n"),
                cfg.httpPort, WiFi.localIP().toString().c_str());
}

void stopHttpService() {
  if (!running || !server) return;
  server->stop();
  delete server;
  server = nullptr;
  running = false;
  Serial.println(TR("[HTTP] Service stopped", "[HTTP] 服务已停止"));
}

void loopHttpService() {
  RuntimeConfig& cfg = getRuntimeConfig();

  // 根据开关和 WiFi 状态自动启停
  if (cfg.httpEnabled && isWiFiConnected() && !running) {
    startHttpService();
  } else if ((!cfg.httpEnabled || !isWiFiConnected()) && running) {
    stopHttpService();
  }

  if (running && server) {
    server->handleClient();
  }
}

bool isHttpServiceRunning() {
  return running;
}
