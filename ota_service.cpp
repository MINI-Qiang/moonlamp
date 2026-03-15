/**
 * OTA 远程固件更新服务 - 实现
 * 
 * 独立模块，可跨项目复用。
 * 通过 OTA_* 宏配置项目/产品/版本信息，不依赖项目特定头文件。
 * 
 * 移植时仅需修改下方的 #include 指向项目自己的配置头文件（定义 OTA_* 宏）。
 */

#include "config.h"       // ← 项目配置（定义 OTA_* 宏），移植时替换为目标项目的配置头
#include "ota_service.h"
#include "config_manager.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_mac.h>

// ============ 内部状态 ============
static uint8_t otaState = OTA_IDLE;
static uint8_t otaProgress = 0;
static OtaUpdateInfo updateInfo;
static OtaStateCallback stateCallback = nullptr;

// 运行时可调参数 (初始化为编译常量，可通过 config_manager 覆盖)
static String rtBaseUrl       = OTA_BASE_URL;
static unsigned long rtCheckInterval = OTA_CHECK_INTERVAL;
static unsigned long rtFirstDelay    = OTA_FIRST_DELAY;

static unsigned long lastCheckTime = 0;
static bool firstCheckDone = false;
static unsigned long bootTime = 0;

// 延迟执行标志（避免在 BLE 回调的 NimBLE 任务栈中执行 HTTPS 操作）
static volatile bool pendingCheck = false;
static volatile bool pendingUpdate = false;

// 缓存 WiFi 连接状态（由 loopOtaService 更新，供 BLE 回调判断）
static volatile bool wifiReady = false;



// NVS 持久化（跨重启保留更新信息）
static Preferences otaPrefs;

// ============ 内部辅助 ============

static String getDeviceMAC() {
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
}

static void setState(uint8_t newState) {
  if (otaState != newState) {
    otaState = newState;
    Serial.printf("[OTA] 状态: 0x%02X\n", newState);
    if (stateCallback) {
      stateCallback(otaState, otaProgress);
    }
  }
}

static void setProgress(uint8_t pct) {
  otaProgress = pct;
  if (stateCallback) {
    stateCallback(otaState, otaProgress);
  }
}

static void setError(uint8_t errCode) {
  otaProgress = 0;
  setState(errCode);
}

// 持久化更新信息到 NVS
static void saveUpdateInfoToNVS() {
  otaPrefs.begin("ota_cfg", false);
  otaPrefs.putBool("hasUpdate", updateInfo.available);
  if (updateInfo.available) {
    otaPrefs.putString("newVer", updateInfo.version);
    otaPrefs.putString("newUrl", updateInfo.url);
    otaPrefs.putString("newMd5", updateInfo.md5);
    otaPrefs.putString("newLog", updateInfo.changelog);
    otaPrefs.putULong("newSize", updateInfo.size);
    otaPrefs.putBool("force", updateInfo.force);
  }
  otaPrefs.end();
}

// 从 NVS 恢复更新信息
static void loadUpdateInfoFromNVS() {
  otaPrefs.begin("ota_cfg", true);
  updateInfo.available = otaPrefs.getBool("hasUpdate", false);
  if (updateInfo.available) {
    updateInfo.version   = otaPrefs.getString("newVer", "");
    updateInfo.url       = otaPrefs.getString("newUrl", "");
    updateInfo.md5       = otaPrefs.getString("newMd5", "");
    updateInfo.changelog = otaPrefs.getString("newLog", "");
    updateInfo.size      = otaPrefs.getULong("newSize", 0);
    updateInfo.force     = otaPrefs.getBool("force", false);
    // 如果数据不完整，清除
    if (updateInfo.version.isEmpty() || updateInfo.url.isEmpty()) {
      updateInfo.available = false;
    }
  }
  otaPrefs.end();
}

static void clearUpdateInfoNVS() {
  otaPrefs.begin("ota_cfg", false);
  otaPrefs.putBool("hasUpdate", false);
  otaPrefs.remove("newVer");
  otaPrefs.remove("newUrl");
  otaPrefs.remove("newMd5");
  otaPrefs.remove("newLog");
  otaPrefs.remove("newSize");
  otaPrefs.remove("force");
  otaPrefs.end();
}

// 前向声明
static void reportResult(const char* event);

// 语义化版本比较: 如果 a > b 返回 true
static bool isVersionNewer(const String &a, const String &b) {
  int aMaj = 0, aMin = 0, aPat = 0;
  int bMaj = 0, bMin = 0, bPat = 0;
  sscanf(a.c_str(), "%d.%d.%d", &aMaj, &aMin, &aPat);
  sscanf(b.c_str(), "%d.%d.%d", &bMaj, &bMin, &bPat);
  if (aMaj != bMaj) return aMaj > bMaj;
  if (aMin != bMin) return aMin > bMin;
  return aPat > bPat;
}

// ============ HTTP 检查更新 ============
static void doCheckUpdate() {
  setState(OTA_CHECKING);

  String url = rtBaseUrl + OTA_API_PATH + "/check";

  // 构造请求 JSON
  JsonDocument reqDoc;
  reqDoc["project_id"]  = OTA_PROJECT_ID;
  reqDoc["product_id"]  = OTA_PRODUCT_ID;
  reqDoc["device_id"]   = getDeviceMAC();
  reqDoc["fw_version"]  = OTA_FW_VERSION;
  reqDoc["hw_version"]  = OTA_HW_VERSION;

  String reqBody;
  serializeJson(reqDoc, reqBody);

  // 发起 HTTPS 请求
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();  // 跳过证书验证（Cloudflare Workers 使用受信任 CA）

  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(15000);

  int httpCode = http.POST(reqBody);
  Serial.printf("[OTA] 检查更新: HTTP %d\n", httpCode);

  if (httpCode != 200) {
    http.end();
    setError(OTA_ERR_HTTP);
    // 错误后短暂停留，然后回到 IDLE 等待下次检查
    delay(100);
    setState(OTA_IDLE);
    return;
  }

  // 解析响应
  String respBody = http.getString();
  http.end();

  JsonDocument respDoc;
  if (deserializeJson(respDoc, respBody)) {
    setError(OTA_ERR_PARSE);
    delay(100);
    setState(OTA_IDLE);
    return;
  }

  bool hasUpdate = respDoc["update"] | false;
  if (!hasUpdate) {
    Serial.println("[OTA] 已是最新版本");
    updateInfo.available = false;
    clearUpdateInfoNVS();
    setState(OTA_NO_UPDATE);
    delay(100);
    setState(OTA_IDLE);
    return;
  }

  // 有更新
  String newVersion = respDoc["version"] | "";
  
  // 客户端侧二次确认版本确实更新（防止服务端逻辑异常）
  if (!isVersionNewer(newVersion, String(OTA_FW_VERSION))) {
    Serial.printf("[OTA] 服务端返回版本 %s 不高于当前 %s，忽略\n", 
                  newVersion.c_str(), OTA_FW_VERSION);
    setState(OTA_NO_UPDATE);
    delay(100);
    setState(OTA_IDLE);
    return;
  }

  updateInfo.available = true;
  updateInfo.version   = newVersion;
  updateInfo.url       = respDoc["url"] | "";
  updateInfo.md5       = respDoc["md5"] | "";
  updateInfo.changelog = respDoc["changelog"] | "";
  updateInfo.size      = respDoc["size"] | 0;
  updateInfo.force     = respDoc["force"] | false;

  Serial.printf("[OTA] 发现新版本: %s → %s (%u 字节)\n",
                OTA_FW_VERSION, updateInfo.version.c_str(), updateInfo.size);
  if (updateInfo.changelog.length() > 0) {
    Serial.printf("[OTA] 更新日志: %s\n", updateInfo.changelog.c_str());
  }

  // 持久化到 NVS
  saveUpdateInfoToNVS();
  setState(OTA_AVAILABLE);
}

// ============ 固件下载与写入 ============
static void doDownloadAndFlash() {
  setState(OTA_DOWNLOADING);
  setProgress(0);

  if (updateInfo.url.isEmpty()) {
    setError(OTA_ERR_DOWNLOAD);
    return;
  }

  Serial.printf("[OTA] 开始下载: %s\n", updateInfo.url.c_str());

  // 初始化 OTA Update
  if (updateInfo.md5.length() == 32) {
    Update.setMD5(updateInfo.md5.c_str());
  }

  uint32_t maxSketchSize = (updateInfo.size > 0) ? updateInfo.size : UPDATE_SIZE_UNKNOWN;
  if (!Update.begin(maxSketchSize)) {
    Serial.printf("[OTA] Update.begin 失败: %s\n", Update.errorString());
    setError(OTA_ERR_FLASH);
    return;
  }

  // HTTPS 流式下载
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  http.begin(client, updateInfo.url);
  http.setTimeout(30000);

  int httpCode = http.GET();
  if (httpCode != 200) {
    Serial.printf("[OTA] 下载失败: HTTP %d\n", httpCode);
    Update.abort();
    http.end();
    setError(OTA_ERR_DOWNLOAD);
    return;
  }

  int totalSize = http.getSize();
  if (totalSize <= 0) totalSize = updateInfo.size;

  WiFiClient *stream = http.getStreamPtr();
  uint8_t buf[1024];
  int written = 0;
  uint8_t lastReportedPct = 0;

  while (http.connected() && (totalSize <= 0 || written < totalSize)) {
    int available = stream->available();
    if (available > 0) {
      int readBytes = stream->readBytes(buf, min(available, (int)sizeof(buf)));
      if (readBytes > 0) {
        size_t writeResult = Update.write(buf, readBytes);
        if (writeResult != (size_t)readBytes) {
          Serial.printf("[OTA] Flash 写入失败 (wrote %d/%d)\n", (int)writeResult, readBytes);
          Update.abort();
          http.end();
          setError(OTA_ERR_FLASH);
          return;
        }
        written += readBytes;

        // 更新进度（每变化 1% 通知一次）
        if (totalSize > 0) {
          uint8_t pct = (uint8_t)((written * 100L) / totalSize);
          if (pct != lastReportedPct) {
            lastReportedPct = pct;
            setProgress(pct);
            if (pct % 10 == 0) {
              Serial.printf("[OTA] 下载进度: %d%%\n", pct);
            }
          }
        }
      }
    }
    yield();
  }

  http.end();

  // 校验并完成
  setState(OTA_VERIFYING);
  if (!Update.end(true)) {
    Serial.printf("[OTA] 校验失败: %s\n", Update.errorString());
    setError(OTA_ERR_MD5);
    return;
  }

  Serial.println("[OTA] 固件写入完成，校验通过");
  setProgress(100);
  setState(OTA_READY);

  // 清除 NVS 更新信息
  clearUpdateInfoNVS();

  // 上报成功（尽力而为，不阻塞）
  reportResult("update_success");

  Serial.println("[OTA] 即将重启...");
  setState(OTA_REBOOTING);
  delay(1000);
  ESP.restart();
}

// ============ 上报更新结果 ============
static void reportResult(const char* event) {
  String url = rtBaseUrl + OTA_API_PATH + "/report";

  JsonDocument doc;
  doc["project_id"]   = OTA_PROJECT_ID;
  doc["product_id"]   = OTA_PRODUCT_ID;
  doc["device_id"]    = getDeviceMAC();
  doc["event"]        = event;
  doc["from_version"] = OTA_FW_VERSION;
  doc["to_version"]   = updateInfo.version;

  String body;
  serializeJson(doc, body);

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);
  int code = http.POST(body);
  http.end();

  Serial.printf("[OTA] 上报 %s: HTTP %d\n", event, code);
}

// ============ 公开接口实现 ============

void otaSetStateCallback(OtaStateCallback cb) {
  stateCallback = cb;
}

void initOtaService() {
  bootTime = millis();
  otaState = OTA_IDLE;
  otaProgress = 0;
  firstCheckDone = false;
  lastCheckTime = 0;

  // 从 config_manager 加载运行时配置
  RuntimeConfig& cfg = getRuntimeConfig();
  if (strlen(cfg.otaBaseUrl) > 0)  rtBaseUrl = cfg.otaBaseUrl;
  if (cfg.otaCheckInterval > 0)    rtCheckInterval = cfg.otaCheckInterval;
  if (cfg.otaFirstDelay > 0)       rtFirstDelay = cfg.otaFirstDelay;

  // 尝试从 NVS 恢复上次检查到的更新信息
  loadUpdateInfoFromNVS();
  if (updateInfo.available) {
    Serial.printf("[OTA] 恢复待更新: %s → %s\n", OTA_FW_VERSION, updateInfo.version.c_str());
    // 再次验证版本（可能已经通过 OTA 升级到该版本了）
    if (!isVersionNewer(updateInfo.version, String(OTA_FW_VERSION))) {
      Serial.println("[OTA] 已更新到该版本，清除");
      updateInfo.available = false;
      clearUpdateInfoNVS();
    } else {
      setState(OTA_AVAILABLE);
    }
  }

  Serial.printf("[OTA] 服务初始化 (项目:%s 产品:%s 硬件:%s 固件:%s)\n",
                OTA_PROJECT_ID, OTA_PRODUCT_ID, OTA_HW_VERSION, OTA_FW_VERSION);
}

void loopOtaService(bool wifiConnected) {
  wifiReady = wifiConnected;
  // 不在 WiFi 连接状态 或 正在下载时不处理定时逻辑
  if (!wifiConnected) return;
  if (otaState == OTA_DOWNLOADING || otaState == OTA_VERIFYING || 
      otaState == OTA_REBOOTING || otaState == OTA_READY) return;

  unsigned long now = millis();

  // 处理延迟执行的检查请求
  if (pendingCheck) {
    pendingCheck = false;
    lastCheckTime = millis();
    firstCheckDone = true;
    doCheckUpdate();
    return;
  }

  // 处理延迟执行的升级请求：保存标志后重启，重启后无 BLE 直接下载
  if (pendingUpdate) {
    pendingUpdate = false;
    Preferences p;
    p.begin("ota_cfg", false);
    p.putBool("doUpdate", true);
    p.end();
    Serial.println("[OTA] 已设置更新标志，即将重启...");
    setState(OTA_REBOOTING);
    delay(500);
    ESP.restart();
    return;
  }

  // 首次检查：等待 rtFirstDelay 后执行
  if (!firstCheckDone) {
    if (now - bootTime >= rtFirstDelay) {
      firstCheckDone = true;
      lastCheckTime = now;
      // 如果 NVS 中已有待更新，跳过首次自动检查
      if (!updateInfo.available) {
        doCheckUpdate();
      }
    }
    return;
  }

  // 定期检查
  if (otaState == OTA_IDLE && (now - lastCheckTime >= rtCheckInterval)) {
    lastCheckTime = now;
    doCheckUpdate();
  }
}

void otaCheckNow() {
  if (otaState == OTA_DOWNLOADING || otaState == OTA_VERIFYING || otaState == OTA_REBOOTING) {
    Serial.println("[OTA] 正在更新中，无法重复检查");
    return;
  }
  if (!wifiReady) {
    Serial.println("[OTA] WiFi 未连接，无法检查更新");
    setError(OTA_ERR_NO_WIFI);
    return;
  }
  // 设置标志，由 loopOtaService() 在主循环中执行（避免 BLE 回调栈溢出）
  pendingCheck = true;
}

void otaStartUpdate() {
  if (otaState != OTA_AVAILABLE) {
    Serial.println("[OTA] 无可用更新");
    return;
  }
  if (updateInfo.url.isEmpty()) {
    Serial.println("[OTA] 更新 URL 为空");
    setError(OTA_ERR_DOWNLOAD);
    return;
  }
  if (!wifiReady) {
    Serial.println("[OTA] WiFi 未连接，无法开始更新");
    setError(OTA_ERR_NO_WIFI);
    return;
  }
  // 设置标志，由 loopOtaService() 在主循环中执行（避免 BLE 回调栈溢出）
  pendingUpdate = true;
}



void otaCancelUpdate() {
  if (otaState == OTA_DOWNLOADING || otaState == OTA_VERIFYING || otaState == OTA_REBOOTING) {
    Serial.println("[OTA] 正在写入中，无法取消");
    return;
  }
  updateInfo.available = false;
  clearUpdateInfoNVS();
  otaProgress = 0;
  setState(OTA_IDLE);
  Serial.println("[OTA] 已取消");
}

uint8_t getOtaState() {
  return otaState;
}

uint8_t getOtaProgress() {
  return otaProgress;
}

const char* getOtaCurrentVersion() {
  return OTA_FW_VERSION;
}

const OtaUpdateInfo& getOtaUpdateInfo() {
  return updateInfo;
}

void otaConfirmIfNeeded() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) return;

  esp_ota_img_states_t imgState;
  if (esp_ota_get_state_partition(running, &imgState) == ESP_OK) {
    if (imgState == ESP_OTA_IMG_PENDING_VERIFY) {
      esp_ota_mark_app_valid_cancel_rollback();
      Serial.printf("[OTA] 新固件 %s 验证通过，已确认分区\n", OTA_FW_VERSION);
      // 上报成功（WiFi 可能还没连接，延迟到 loop 中处理）
    }
  }

  Serial.printf("[OTA] 运行分区: %s (地址: 0x%06X)\n",
                running->label, (unsigned int)running->address);
}

// ============ 重启后 OTA 下载 ============

bool otaHasPendingUpdate() {
  Preferences p;
  p.begin("ota_cfg", true);
  bool pending = p.getBool("doUpdate", false);
  p.end();
  return pending;
}

void otaRunPendingUpdate() {
  // 立即清除标志，防止失败后循环重启
  Preferences p;
  p.begin("ota_cfg", false);
  p.putBool("doUpdate", false);
  p.end();

  // 初始化 OTA 服务（从 NVS 加载更新信息）
  initOtaService();

  if (!updateInfo.available || updateInfo.url.isEmpty()) {
    Serial.println("[OTA] 无有效的更新信息，正常启动");
    return;
  }

  Serial.printf("[OTA] 重启后 OTA 模式: %s -> %s\n",
                OTA_FW_VERSION, updateInfo.version.c_str());
  Serial.printf("[OTA] 可用堆: %u 字节（无 BLE 开销）\n", ESP.getFreeHeap());

  doDownloadAndFlash();
  // 成功则 ESP.restart()，失败则 return 由调用方继续正常启动
}
