#include "ble_service.h"
#include "led_effects.h"
#include "wifi_service.h"
#include "time_service.h"
#include "time_effect.h"
#include "sun_calc.h"
#include "esp_mac.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

// ============ BLE回调 ============
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *s) override {
    deviceConnected = true;
    Serial.println("[BLE] 客户端已连接");
  }
  void onDisconnect(BLEServer *s) override {
    deviceConnected = false;
    needRestart = true;
    Serial.println("[BLE] 客户端已断开");
  }
};

class HSVWriteCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pChar) override {
    String val = pChar->getValue();
    if (val.length() >= 3) {
      currentH = (uint8_t)val[0];
      currentS = (uint8_t)val[1];
      currentV = (uint8_t)val[2];
      Serial.printf("[BLE] 收到HSV: H=%d S=%d V=%d\n", currentH, currentS, currentV);
      needApplyLED = true;
      syncCharacteristics();
      if (deviceConnected) pCharHSV->notify();
    }
  }
};

class PowerWriteCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pChar) override {
    String val = pChar->getValue();
    if (val.length() >= 1) {
      powerOn = ((uint8_t)val[0] != 0);
      Serial.printf("[BLE] 收到开关: %s\n", powerOn ? "开" : "关");
      needApplyLED = true;
      syncCharacteristics();
      if (deviceConnected) pCharPower->notify();
    }
  }
};

class EffectWriteCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pChar) override {
    String val = pChar->getValue();
    if (val.length() >= 1) {
      effectMode = (uint8_t)val[0];
      Serial.printf("[BLE] 收到灯效模式: %d\n", effectMode);
      needApplyLED = true;
      syncCharacteristics();
      if (deviceConnected) pCharEffect->notify();
    }
  }
};

class ParamWriteCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pChar) override {
    String val = pChar->getValue();
    if (val.length() >= 1) effectSpeed = (uint8_t)val[0];
    if (val.length() >= 2) effectParam1 = (uint8_t)val[1];
    if (val.length() >= 3) effectParam2 = (uint8_t)val[2];
    Serial.printf("[BLE] 收到灯效参数: Speed=%d P1=%d P2=%d\n", effectSpeed, effectParam1, effectParam2);
    syncCharacteristics();
    if (deviceConnected) pCharParam->notify();
  }
};

// ============ 时间灯效配置回调 ============
class TimeFxCallback : public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic *pChar) override {
    // 读取时返回配置 + 状态 (14 + 4 = 18字节)
    uint8_t buf[18];
    serializeTimeEffectConfig(buf);
    getTimeEffectStatus(buf + 14);
    pChar->setValue(buf, 18);
  }
  void onWrite(BLECharacteristic *pChar) override {
    String val = pChar->getValue();
    if (deserializeTimeEffectConfig((const uint8_t*)val.c_str(), val.length())) {
      saveTimeEffectConfig();
      if (deviceConnected && pCharTimeFx) pCharTimeFx->notify();
    }
  }
};

// ============ WiFi配网回调 ============
class WiFiCfgCallback : public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic *pChar) override {
    String ssid = getStoredSSID();
    pChar->setValue(ssid.c_str());
  }
  void onWrite(BLECharacteristic *pChar) override {
    String val = pChar->getValue();
    // 写入单字节 0x00 → 进入安全配网模式（清除凭据+重启）
    if (val.length() == 1 && (uint8_t)val[0] == 0x00) {
      Serial.println("[BLE] 收到重新配网请求");
      enterProvisioningMode();
      return;
    }
    int sep = val.indexOf('\n');
    if (sep <= 0) {
      Serial.println("[BLE] WiFi配置格式错误 (需要 SSID\\nPASSWORD)");
      return;
    }
    String ssid = val.substring(0, sep);
    String pass = (sep + 1 < (int)val.length()) ? val.substring(sep + 1) : "";
    if (setWiFiCredentials(ssid, pass)) {
      Serial.printf("[BLE] WiFi配置已更新: SSID=%s\n", ssid.c_str());
    }
  }
};

// ============ 时间同步回调 ============
class TimeSyncCallback : public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic *pChar) override {
    unsigned long epoch = getEpochTime();
    uint8_t data[4];
    data[0] = epoch & 0xFF;
    data[1] = (epoch >> 8) & 0xFF;
    data[2] = (epoch >> 16) & 0xFF;
    data[3] = (epoch >> 24) & 0xFF;
    pChar->setValue(data, 4);
  }
  void onWrite(BLECharacteristic *pChar) override {
    String val = pChar->getValue();
    if (val.length() >= 4) {
      unsigned long epoch = (unsigned long)(uint8_t)val[0] |
                           ((unsigned long)(uint8_t)val[1] << 8) |
                           ((unsigned long)(uint8_t)val[2] << 16) |
                           ((unsigned long)(uint8_t)val[3] << 24);
      setTimeFromEpoch(epoch);
      if (deviceConnected) pCharTimeSync->notify();
    }
  }
};

// ============ CTS标准时间回调 ============
class CTSTimeCallback : public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic *pChar) override {
    uint8_t buf[10];
    buildCTSCurrentTime(buf);
    pChar->setValue(buf, 10);
  }
  void onWrite(BLECharacteristic *pChar) override {
    String val = pChar->getValue();
    if (parseCTSCurrentTime((const uint8_t *)val.c_str(), val.length())) {
      // 同步自定义时间特征值
      if (deviceConnected && pCharTimeSync) {
        unsigned long epoch = getEpochTime();
        uint8_t data[4];
        data[0] = epoch & 0xFF;
        data[1] = (epoch >> 8) & 0xFF;
        data[2] = (epoch >> 16) & 0xFF;
        data[3] = (epoch >> 24) & 0xFF;
        pCharTimeSync->setValue(data, 4);
        pCharTimeSync->notify();
      }
      if (deviceConnected && pCharCTSTime) pCharCTSTime->notify();
    }
  }
};

// ============ 生成BLE广播名称 ============
static String makeBLEName() {
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  char name[20];
  snprintf(name, sizeof(name), DEVICE_PREFIX "_%02X%02X%02X", mac[3], mac[4], mac[5]);
  return String(name);
}

// ============ 同步特征值 ============
void syncCharacteristics() {
  uint8_t hsv[3] = { currentH, currentS, currentV };
  pCharHSV->setValue(hsv, 3);
  uint8_t pw = powerOn ? 1 : 0;
  pCharPower->setValue(&pw, 1);
  pCharEffect->setValue(&effectMode, 1);
  uint8_t params[3] = { effectSpeed, effectParam1, effectParam2 };
  pCharParam->setValue(params, 3);
}

// ============ WiFi/Time状态同步 ============
void syncWiFiTimeCharacteristics() {
  static uint8_t lastWifiSt = 0xFF;
  uint8_t wifiSt = getWiFiStatus();
  if (wifiSt != lastWifiSt) {
    lastWifiSt = wifiSt;
    if (pCharWiFiStatus) {
      pCharWiFiStatus->setValue(&wifiSt, 1);
      if (deviceConnected) pCharWiFiStatus->notify();
    }
  }
}

// ============ 初始化BLE ============
void setupBLE() {
  String bleName = makeBLEName();
  Serial.printf("[BLE] 广播名称: %s\n", bleName.c_str());

  BLEDevice::init(bleName.c_str());
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  // ===== LED灯光服务 =====
  BLEService *pService = pServer->createService(SERVICE_UUID);

  pCharHSV = pService->createCharacteristic(
    CHAR_HSV_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharHSV->setCallbacks(new HSVWriteCallback());

  pCharPower = pService->createCharacteristic(
    CHAR_POWER_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharPower->setCallbacks(new PowerWriteCallback());

  pCharEffect = pService->createCharacteristic(
    CHAR_EFFECT_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharEffect->setCallbacks(new EffectWriteCallback());

  pCharParam = pService->createCharacteristic(
    CHAR_PARAM_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharParam->setCallbacks(new ParamWriteCallback());

  pCharTimeFx = pService->createCharacteristic(
    CHAR_TIME_FX_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharTimeFx->setCallbacks(new TimeFxCallback());

  // ===== WiFi配网服务 =====
  BLEService *pWiFiService = pServer->createService(WIFI_SERVICE_UUID);

  pCharWiFiCfg = pWiFiService->createCharacteristic(
    CHAR_WIFI_CFG_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE
  );
  pCharWiFiCfg->setCallbacks(new WiFiCfgCallback());

  pCharWiFiStatus = pWiFiService->createCharacteristic(
    CHAR_WIFI_STATUS_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_NOTIFY
  );

  // ===== 时间服务 =====
  BLEService *pTimeService = pServer->createService(TIME_SERVICE_UUID);

  pCharTimeSync = pTimeService->createCharacteristic(
    CHAR_TIME_SYNC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharTimeSync->setCallbacks(new TimeSyncCallback());

  // ===== BLE标准CTS (Current Time Service 0x1805) =====
  BLEService *pCTSService = pServer->createService(CTS_SERVICE_UUID);

  pCharCTSTime = pCTSService->createCharacteristic(
    CTS_CURRENT_TIME_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharCTSTime->setCallbacks(new CTSTimeCallback());

  pCharCTSLocal = pCTSService->createCharacteristic(
    CTS_LOCAL_TIME_UUID,
    BLECharacteristic::PROPERTY_READ
  );

  // ===== 同步初始值并启动所有服务 =====
  syncCharacteristics();

  uint8_t wifiSt = getWiFiStatus();
  pCharWiFiStatus->setValue(&wifiSt, 1);
  String ssid = getStoredSSID();
  if (ssid.length() > 0) pCharWiFiCfg->setValue(ssid.c_str());

  uint8_t ctsBuf[10];
  buildCTSCurrentTime(ctsBuf);
  pCharCTSTime->setValue(ctsBuf, 10);
  uint8_t localBuf[2];
  buildCTSLocalTime(localBuf);
  pCharCTSLocal->setValue(localBuf, 2);

  pService->start();
  pWiFiService->start();
  pTimeService->start();
  pCTSService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();
}
