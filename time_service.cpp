#include "time_service.h"
#include "config_manager.h"
#include "wifi_service.h"
#include <ESP32Time.h>
#include <esp_sntp.h>

// offset=0: 时区由configTime设置，ESP32Time不额外偏移
static ESP32Time rtc(0);
static bool timeSynced = false;
static unsigned long lastNTPSync = 0;
static bool wasWiFiConnected = false;
static bool ntpPending = false;
static bool sntpInitialized = false;

#define NTP_SYNC_INTERVAL_MS 86400000UL  // 24小时 (ms)

// SNTP同步完成回调（在LWIP线程中调用）
static void ntpSyncCallback(struct timeval *tv) {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 0)) {
    rtc.setTimeStruct(timeinfo);
    timeSynced = true;
    ntpPending = false;
    lastNTPSync = millis();
    Serial.printf("[Time] NTP异步同步成功: %s\n", rtc.getDateTime().c_str());
  }
}

void initTimeService() {
  esp_sntp_set_time_sync_notification_cb(ntpSyncCallback);
  Serial.println("[Time] 时间服务初始化 (异步NTP)");
}

void loopTimeService() {
  bool connected = isWiFiConnected();

  // WiFi刚连接，立即触发NTP同步
  if (connected && !wasWiFiConnected) {
    syncNTP();
  }
  wasWiFiConnected = connected;

  // 每日定时重新同步
  if (connected && timeSynced && !ntpPending) {
    if (millis() - lastNTPSync >= NTP_SYNC_INTERVAL_MS) {
      Serial.println("[Time] 每日NTP定时同步");
      syncNTP();
    }
  }
}

bool syncNTP() {
  if (!isWiFiConnected()) {
    Serial.println("[Time] NTP同步跳过: WiFi未连接");
    return false;
  }

  if (ntpPending) {
    Serial.println("[Time] NTP同步已在进行中");
    return false;
  }

  Serial.println("[Time] 发起NTP异步同步...");
  ntpPending = true;

  // 如已初始化SNTP，先停止再重新启动以触发新的同步
  if (sntpInitialized) {
    esp_sntp_stop();
  }
  RuntimeConfig& cfg = getRuntimeConfig();
  configTime(cfg.gmtOffset, cfg.daylightOffset, cfg.ntpServer1, cfg.ntpServer2);
  sntpInitialized = true;

  return true;  // 请求已发出，结果通过回调通知
}

void setTimeFromEpoch(unsigned long epoch) {
  rtc.setTime(epoch);
  timeSynced = true;
  Serial.printf("[Time] BLE授时成功: %s\n", rtc.getDateTime().c_str());
}

unsigned long getEpochTime() {
  return rtc.getEpoch();
}

String getFormattedTime() {
  return rtc.getTime();
}

String getFormattedDateTime() {
  return rtc.getDateTime();
}

bool isTimeSynced() {
  return timeSynced;
}

// ============ CTS标准格式 ============
// Current Time: 10 bytes
//   [0-1] Year (uint16 LE)  [2] Month(1-12)  [3] Day(1-31)
//   [4] Hours(0-23)  [5] Minutes(0-59)  [6] Seconds(0-59)
//   [7] DayOfWeek(1=Mon..7=Sun, 0=unknown)  [8] Fractions256  [9] AdjustReason
void buildCTSCurrentTime(uint8_t *buf) {
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  uint16_t year = timeinfo.tm_year + 1900;
  buf[0] = year & 0xFF;
  buf[1] = (year >> 8) & 0xFF;
  buf[2] = timeinfo.tm_mon + 1;     // tm_mon: 0-11 → 1-12
  buf[3] = timeinfo.tm_mday;
  buf[4] = timeinfo.tm_hour;
  buf[5] = timeinfo.tm_min;
  buf[6] = timeinfo.tm_sec;
  // CTS: 1=Monday..7=Sunday; tm_wday: 0=Sunday..6=Saturday
  buf[7] = (timeinfo.tm_wday == 0) ? 7 : timeinfo.tm_wday;
  buf[8] = 0;  // Fractions256
  buf[9] = 0;  // Adjust Reason
}

bool parseCTSCurrentTime(const uint8_t *buf, size_t len) {
  if (len < 7) return false;
  uint16_t year = buf[0] | ((uint16_t)buf[1] << 8);
  uint8_t month = buf[2];
  uint8_t day   = buf[3];
  uint8_t hour  = buf[4];
  uint8_t min   = buf[5];
  uint8_t sec   = buf[6];
  if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31) return false;
  struct tm t = {};
  t.tm_year = year - 1900;
  t.tm_mon  = month - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min  = min;
  t.tm_sec  = sec;
  rtc.setTimeStruct(t);
  timeSynced = true;
  Serial.printf("[Time] CTS授时成功: %04d-%02d-%02d %02d:%02d:%02d\n", year, month, day, hour, min, sec);
  return true;
}

void buildCTSLocalTime(uint8_t *buf) {
  RuntimeConfig& cfg = getRuntimeConfig();
  // Time Zone: UTC offset in 15-min units (int8). UTC+8 = +32
  buf[0] = (int8_t)(cfg.gmtOffset / 900);
  // DST Offset: 0=standard, 2=+0.5h, 4=+1h, 8=+2h, 255=unknown
  buf[1] = (cfg.daylightOffset == 0) ? 0 : 4;
}
