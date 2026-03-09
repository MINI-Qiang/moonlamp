#ifndef SUN_CALC_H
#define SUN_CALC_H

#include "config.h"

// ============ 日出日落计算 ============
// 基于简化算法计算北京时间(UTC+8)的日出日落时间
// 后期可扩展为基于GPS坐标的精确计算

// 默认坐标：北京 (纬度39.9, 经度116.4)
// 可通过 setSunLocation() 修改
#define DEFAULT_LATITUDE   39.9f
#define DEFAULT_LONGITUDE  116.4f

// 设置地理位置（纬度、经度）
void setSunLocation(float latitude, float longitude);

// 获取当前设置的位置
float getSunLatitude();
float getSunLongitude();

// 计算指定日期的日出时间（返回当天分钟数 0-1439）
// year: 年份, month: 1-12, day: 1-31
uint16_t calcSunrise(int year, int month, int day);

// 计算指定日期的日落时间（返回当天分钟数 0-1439）
uint16_t calcSunset(int year, int month, int day);

// 获取今天的日出时间（分钟）- 需要时间已同步
uint16_t getTodaySunrise();

// 获取今天的日落时间（分钟）- 需要时间已同步  
uint16_t getTodaySunset();

// 将分钟数转换为 "HH:MM" 字符串
String minutesToTimeStr(uint16_t minutes);

#endif
