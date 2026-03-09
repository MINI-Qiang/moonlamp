#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include "config.h"

// 初始化BLE服务 (创建Server、Service、Characteristics，开始广播)
void setupBLE();

// 同步所有特征值 (用于Notify/Read)
void syncCharacteristics();

// 检查WiFi/Time状态变化并通知BLE客户端
void syncWiFiTimeCharacteristics();

#endif
