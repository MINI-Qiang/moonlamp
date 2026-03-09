#include "sun_calc.h"
#include "time_service.h"
#include <math.h>

// 地理位置
static float sunLatitude = DEFAULT_LATITUDE;
static float sunLongitude = DEFAULT_LONGITUDE;

// 角度转弧度
#define DEG_TO_RAD 0.017453292519943295
#define RAD_TO_DEG 57.29577951308232

void setSunLocation(float latitude, float longitude) {
  sunLatitude = latitude;
  sunLongitude = longitude;
  Serial.printf("[Sun] 位置已更新: 纬度=%.2f, 经度=%.2f\n", latitude, longitude);
}

float getSunLatitude() { return sunLatitude; }
float getSunLongitude() { return sunLongitude; }

// ============ 日出日落计算核心算法 ============
// 基于NOAA简化算法，精度约±5分钟

// 计算儒略日
static double toJulianDay(int year, int month, int day) {
  if (month <= 2) {
    year -= 1;
    month += 12;
  }
  int A = year / 100;
  int B = 2 - A + (A / 4);
  return floor(365.25 * (year + 4716)) + floor(30.6001 * (month + 1)) + day + B - 1524.5;
}

// 计算日出或日落时间（分钟数）
// isSunrise: true=日出, false=日落
static uint16_t calcSunTime(int year, int month, int day, bool isSunrise) {
  double JD = toJulianDay(year, month, day);
  double n = JD - 2451545.0 + 0.0008;  // 自J2000的天数
  
  // 平太阳经度
  double Jstar = n - (sunLongitude / 360.0);
  double M = fmod(357.5291 + 0.98560028 * Jstar, 360.0);  // 平近点角
  double Mrad = M * DEG_TO_RAD;
  
  // 中心方程
  double C = 1.9148 * sin(Mrad) + 0.0200 * sin(2 * Mrad) + 0.0003 * sin(3 * Mrad);
  
  // 黄经
  double lambda = fmod(M + C + 180.0 + 102.9372, 360.0);
  double lambdaRad = lambda * DEG_TO_RAD;
  
  // 太阳赤纬
  double delta = asin(sin(lambdaRad) * sin(23.44 * DEG_TO_RAD));
  
  // 时角
  double latRad = sunLatitude * DEG_TO_RAD;
  double cosOmega = (sin(-0.833 * DEG_TO_RAD) - sin(latRad) * sin(delta)) / (cos(latRad) * cos(delta));
  
  // 极昼/极夜检查
  if (cosOmega > 1.0) {
    // 极夜：太阳不升起
    return isSunrise ? 720 : 720;  // 返回中午，实际应处理极夜情况
  }
  if (cosOmega < -1.0) {
    // 极昼：太阳不落下
    return isSunrise ? 0 : 1439;
  }
  
  double omega = acos(cosOmega) * RAD_TO_DEG;
  
  // 太阳中天时间（J_transit）
  double Jtransit = 2451545.0 + Jstar + 0.0053 * sin(Mrad) - 0.0069 * sin(2 * lambdaRad);
  
  // 日出/日落儒略日
  double Jrise = Jtransit - (omega / 360.0);
  double Jset = Jtransit + (omega / 360.0);
  
  // 转换为UTC时间（小时）
  double targetJ = isSunrise ? Jrise : Jset;
  double utcHours = (targetJ - JD + 0.5) * 24.0;
  
  // 转换为本地时间（UTC+8）
  double localHours = utcHours + 8.0;
  if (localHours < 0) localHours += 24.0;
  if (localHours >= 24) localHours -= 24.0;
  
  uint16_t minutes = (uint16_t)(localHours * 60.0);
  if (minutes > 1439) minutes = 1439;
  
  return minutes;
}

uint16_t calcSunrise(int year, int month, int day) {
  return calcSunTime(year, month, day, true);
}

uint16_t calcSunset(int year, int month, int day) {
  return calcSunTime(year, month, day, false);
}

uint16_t getTodaySunrise() {
  if (!isTimeSynced()) return 360;  // 默认6:00
  
  unsigned long epoch = getEpochTime();
  // 转换为北京时间
  time_t localTime = epoch + 8 * 3600;
  struct tm* ti = gmtime(&localTime);
  
  return calcSunrise(ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday);
}

uint16_t getTodaySunset() {
  if (!isTimeSynced()) return 1080;  // 默认18:00
  
  unsigned long epoch = getEpochTime();
  time_t localTime = epoch + 8 * 3600;
  struct tm* ti = gmtime(&localTime);
  
  return calcSunset(ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday);
}

String minutesToTimeStr(uint16_t minutes) {
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
  return String(buf);
}
