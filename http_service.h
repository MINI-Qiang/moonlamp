#ifndef HTTP_SERVICE_H
#define HTTP_SERVICE_H

/**
 * HTTP 局域网配置服务
 * 
 * 提供 RESTful API，允许局域网内其他设备/智能家居系统
 * 读取和配置设备参数，实现与 BLE/串口 同等的配置能力。
 * 
 * 默认关闭，由用户通过串口或 BLE 主动开启。
 * 
 * 端点:
 *   GET  /api/status     - 设备综合状态
 *   GET  /api/config     - 系统运行时配置
 *   PUT  /api/config     - 修改系统配置
 *   GET  /api/led        - LED 灯光状态
 *   PUT  /api/led        - 设置灯光
 *   GET  /api/effect     - 灯效模式与参数
 *   PUT  /api/effect     - 设置灯效
 *   GET  /api/timefx     - 时间灯效配置
 *   PUT  /api/timefx     - 设置时间灯效配置
 *   GET  /api/wifi       - WiFi 连接状态
 *   GET  /api/ota        - OTA 状态
 *   POST /api/ota/check  - 手动检查更新
 *   POST /api/ota/update - 确认升级
 *   POST /api/ota/cancel - 取消更新
 */

#include <Arduino.h>

// 初始化 HTTP 服务 (根据 config 中 httpEnabled 决定是否启动)
void initHttpService();

// HTTP 服务循环 (在 loop 中调用)
void loopHttpService();

// 启动 HTTP 服务
void startHttpService();

// 停止 HTTP 服务
void stopHttpService();

// HTTP 服务是否正在运行
bool isHttpServiceRunning();

#endif
