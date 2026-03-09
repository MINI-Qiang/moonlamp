#ifndef TIME_SERVICE_H
#define TIME_SERVICE_H

#include "config.h"

// 初始化时间服务
void initTimeService();

// 时间服务循环（自动NTP同步）
void loopTimeService();

// 手动触发NTP同步（阻塞，最多5秒）
bool syncNTP();

// 通过epoch设置时间（UTC秒）
void setTimeFromEpoch(unsigned long epoch);

// 获取当前UTC epoch
unsigned long getEpochTime();

// 获取格式化时间 "HH:MM:SS"
String getFormattedTime();

// 获取格式化日期时间 "Sun, Jan 17 2021 15:24:38"
String getFormattedDateTime();

// 时间是否已同步
bool isTimeSynced();

// 构建CTS Current Time数据 (10字节)
void buildCTSCurrentTime(uint8_t *buf);

// 解析CTS Current Time数据并设置时间
bool parseCTSCurrentTime(const uint8_t *buf, size_t len);

// 构建CTS Local Time Information (2字节)
void buildCTSLocalTime(uint8_t *buf);

#endif
