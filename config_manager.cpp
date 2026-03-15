#include "config_manager.h"
#include "config.h"
#include "i18n.h"
#include <Preferences.h>
#include <ArduinoJson.h>

static RuntimeConfig rtCfg;
static Preferences cfgPrefs;

// ============ i18n 实现 ============
Lang getLang() {
  return rtCfg.language;
}

void setLang(Lang lang) {
  rtCfg.language = lang;
}

void initConfigManager() {
  cfgPrefs.begin("rt_cfg", false);

  // NTP
  strncpy(rtCfg.ntpServer1,
          cfgPrefs.getString("ntpSrv1", NTP_SERVER1).c_str(),
          sizeof(rtCfg.ntpServer1) - 1);
  strncpy(rtCfg.ntpServer2,
          cfgPrefs.getString("ntpSrv2", NTP_SERVER2).c_str(),
          sizeof(rtCfg.ntpServer2) - 1);
  rtCfg.gmtOffset       = cfgPrefs.getLong("gmtOff", NTP_GMT_OFFSET);
  rtCfg.daylightOffset  = cfgPrefs.getInt("dstOff", NTP_DAYLIGHT_OFFSET);

  // Device
  strncpy(rtCfg.devicePrefix,
          cfgPrefs.getString("devPfx", DEVICE_PREFIX).c_str(),
          sizeof(rtCfg.devicePrefix) - 1);

  // Storage
  rtCfg.saveCheckInterval = cfgPrefs.getULong("saveIntv", SAVE_CHECK_INTERVAL);

  // OTA
  strncpy(rtCfg.otaBaseUrl,
          cfgPrefs.getString("otaUrl", OTA_BASE_URL).c_str(),
          sizeof(rtCfg.otaBaseUrl) - 1);
  rtCfg.otaCheckInterval = cfgPrefs.getULong("otaChkI", OTA_CHECK_INTERVAL);
  rtCfg.otaFirstDelay    = cfgPrefs.getULong("otaFstD", OTA_FIRST_DELAY);

  // HTTP
  rtCfg.httpEnabled = cfgPrefs.getBool("httpEn", false);
  rtCfg.httpPort    = cfgPrefs.getUShort("httpPort", 80);

  // Language
  rtCfg.language = (Lang)cfgPrefs.getUChar("lang", LANG_EN);

  cfgPrefs.end();

  Serial.println("[CFG] Runtime config loaded");
}

RuntimeConfig& getRuntimeConfig() {
  return rtCfg;
}

void saveRuntimeConfig() {
  cfgPrefs.begin("rt_cfg", false);

  cfgPrefs.putString("ntpSrv1", rtCfg.ntpServer1);
  cfgPrefs.putString("ntpSrv2", rtCfg.ntpServer2);
  cfgPrefs.putLong("gmtOff",    rtCfg.gmtOffset);
  cfgPrefs.putInt("dstOff",     rtCfg.daylightOffset);
  cfgPrefs.putString("devPfx",  rtCfg.devicePrefix);
  cfgPrefs.putULong("saveIntv", rtCfg.saveCheckInterval);
  cfgPrefs.putString("otaUrl",  rtCfg.otaBaseUrl);
  cfgPrefs.putULong("otaChkI",  rtCfg.otaCheckInterval);
  cfgPrefs.putULong("otaFstD",  rtCfg.otaFirstDelay);
  cfgPrefs.putBool("httpEn",    rtCfg.httpEnabled);
  cfgPrefs.putUShort("httpPort", rtCfg.httpPort);
  cfgPrefs.putUChar("lang",      (uint8_t)rtCfg.language);

  cfgPrefs.end();
  Serial.println(TR("[CFG] Runtime config saved", "[CFG] 运行时配置已保存"));
}

String configToJson() {
  JsonDocument doc;

  // NTP
  doc["ntp_server1"]       = rtCfg.ntpServer1;
  doc["ntp_server2"]       = rtCfg.ntpServer2;
  doc["ntp_gmt_offset"]    = rtCfg.gmtOffset;
  doc["ntp_daylight_offset"] = rtCfg.daylightOffset;

  // Device
  doc["device_prefix"]     = rtCfg.devicePrefix;

  // Storage
  doc["save_check_interval"] = rtCfg.saveCheckInterval;

  // OTA (可写)
  doc["ota_base_url"]      = rtCfg.otaBaseUrl;
  doc["ota_check_interval"] = rtCfg.otaCheckInterval;
  doc["ota_first_delay"]   = rtCfg.otaFirstDelay;

  // OTA (只读编译时常量)
  doc["ota_project_id"]    = OTA_PROJECT_ID;
  doc["ota_product_id"]    = OTA_PRODUCT_ID;
  doc["ota_fw_version"]    = OTA_FW_VERSION;
  doc["ota_hw_version"]    = OTA_HW_VERSION;

  // HTTP
  doc["http_enabled"]      = rtCfg.httpEnabled;
  doc["http_port"]         = rtCfg.httpPort;

  // Language
  doc["language"]          = (rtCfg.language == LANG_ZH) ? "zh" : "en";

  String out;
  serializeJson(doc, out);
  return out;
}

bool configFromJson(const String& json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;

  bool changed = false;

  if (doc.containsKey("ntp_server1")) {
    strncpy(rtCfg.ntpServer1, doc["ntp_server1"] | "", sizeof(rtCfg.ntpServer1) - 1);
    changed = true;
  }
  if (doc.containsKey("ntp_server2")) {
    strncpy(rtCfg.ntpServer2, doc["ntp_server2"] | "", sizeof(rtCfg.ntpServer2) - 1);
    changed = true;
  }
  if (doc.containsKey("ntp_gmt_offset")) {
    rtCfg.gmtOffset = doc["ntp_gmt_offset"];
    changed = true;
  }
  if (doc.containsKey("ntp_daylight_offset")) {
    rtCfg.daylightOffset = doc["ntp_daylight_offset"];
    changed = true;
  }
  if (doc.containsKey("device_prefix")) {
    strncpy(rtCfg.devicePrefix, doc["device_prefix"] | "", sizeof(rtCfg.devicePrefix) - 1);
    changed = true;
  }
  if (doc.containsKey("save_check_interval")) {
    rtCfg.saveCheckInterval = doc["save_check_interval"];
    changed = true;
  }
  if (doc.containsKey("ota_base_url")) {
    strncpy(rtCfg.otaBaseUrl, doc["ota_base_url"] | "", sizeof(rtCfg.otaBaseUrl) - 1);
    changed = true;
  }
  if (doc.containsKey("ota_check_interval")) {
    rtCfg.otaCheckInterval = doc["ota_check_interval"];
    changed = true;
  }
  if (doc.containsKey("ota_first_delay")) {
    rtCfg.otaFirstDelay = doc["ota_first_delay"];
    changed = true;
  }
  if (doc.containsKey("http_enabled")) {
    rtCfg.httpEnabled = doc["http_enabled"];
    changed = true;
  }
  if (doc.containsKey("http_port")) {
    rtCfg.httpPort = doc["http_port"];
    changed = true;
  }
  if (doc.containsKey("language")) {
    String lang = doc["language"] | "en";
    rtCfg.language = (lang == "zh") ? LANG_ZH : LANG_EN;
    changed = true;
  }

  if (changed) {
    saveRuntimeConfig();
  }
  return changed;
}
