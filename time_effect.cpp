#include "time_effect.h"
#include "sun_calc.h"
#include "time_service.h"
#include "i18n.h"
#include <Preferences.h>

// ============ 全局配置 ============
static TimeEffectConfig teConfig = {
  // 默认配置: H=206(291°), S=0, V=255
  .hue = 206,
  .saturation = 0,
  .maxBrightness = 255,
  .nightBrightness = 30,
  .startTime = -1,       // 使用日落时间
  .peakTime = 1260,      // 21:00
  .nightTime = 1290,     // 21:30
  .offTime = -1,         // 使用日出前30分钟
  .fadeUpDuration = 0,   // 自动计算
  .fadeDownDuration = 30 // 30分钟渐暗
};

static uint8_t currentPhase = 0;
static Preferences tePrefs;

// ============ 配置管理 ============
void initTimeEffect() {
  loadTimeEffectConfig();
  Serial.println(TR("[TimeEffect] System initialized", "[TimeEffect] 时间灯效系统初始化完成"));
}

TimeEffectConfig& getTimeEffectConfig() {
  return teConfig;
}

void setTimeEffectConfig(const TimeEffectConfig& cfg) {
  teConfig = cfg;
}

void saveTimeEffectConfig() {
  tePrefs.begin("time_fx", false);
  tePrefs.putUChar("hue", teConfig.hue);
  tePrefs.putUChar("sat", teConfig.saturation);
  tePrefs.putUChar("maxV", teConfig.maxBrightness);
  tePrefs.putUChar("nightV", teConfig.nightBrightness);
  tePrefs.putShort("startT", teConfig.startTime);
  tePrefs.putShort("peakT", teConfig.peakTime);
  tePrefs.putShort("nightT", teConfig.nightTime);
  tePrefs.putShort("offT", teConfig.offTime);
  tePrefs.putUChar("fadeUp", teConfig.fadeUpDuration);
  tePrefs.putUChar("fadeDown", teConfig.fadeDownDuration);
  tePrefs.end();
  Serial.println(TR("[TimeEffect] Config saved", "[TimeEffect] 配置已保存"));
}

void loadTimeEffectConfig() {
  tePrefs.begin("time_fx", true);
  teConfig.hue = tePrefs.getUChar("hue", 206);
  teConfig.saturation = tePrefs.getUChar("sat", 0);
  teConfig.maxBrightness = tePrefs.getUChar("maxV", 255);
  teConfig.nightBrightness = tePrefs.getUChar("nightV", 30);
  teConfig.startTime = tePrefs.getShort("startT", -1);
  teConfig.peakTime = tePrefs.getShort("peakT", 1260);
  teConfig.nightTime = tePrefs.getShort("nightT", 1290);
  teConfig.offTime = tePrefs.getShort("offT", -1);
  teConfig.fadeUpDuration = tePrefs.getUChar("fadeUp", 0);
  teConfig.fadeDownDuration = tePrefs.getUChar("fadeDown", 30);
  tePrefs.end();
  
  Serial.printf(TR("[TimeEffect] Config loaded: H=%d S=%d maxV=%d nightV=%d\n",
                "[TimeEffect] 配置已加载: H=%d S=%d maxV=%d nightV=%d\n"),
                teConfig.hue, teConfig.saturation, teConfig.maxBrightness, teConfig.nightBrightness);
  Serial.printf(TR("[TimeEffect] Times: start=%d peak=%d night=%d off=%d\n",
                "[TimeEffect] 时间: start=%d peak=%d night=%d off=%d\n"),
                teConfig.startTime, teConfig.peakTime, teConfig.nightTime, teConfig.offTime);
}

// ============ 时间计算辅助 ============

// 获取当前本地时间（分钟数 0-1439）
static int16_t getCurrentMinutes() {
  if (!isTimeSynced()) return -1;
  
  unsigned long epoch = getEpochTime();
  time_t localTime = epoch + 8 * 3600;  // UTC+8
  struct tm* ti = gmtime(&localTime);
  
  return ti->tm_hour * 60 + ti->tm_min;
}

// 获取实际的开始时间（处理-1情况）
static int16_t getActualStartTime() {
  if (teConfig.startTime >= 0) return teConfig.startTime;
  return getTodaySunset();
}

// 获取实际的关闭时间（处理-1情况）
static int16_t getActualOffTime() {
  if (teConfig.offTime >= 0) return teConfig.offTime;
  int16_t sunrise = getTodaySunrise();
  return (sunrise >= 30) ? (sunrise - 30) : (sunrise + 1410);  // 日出前30分钟
}

// 判断时间是否在范围内（处理跨午夜情况）
static bool isTimeInRange(int16_t current, int16_t start, int16_t end) {
  if (start <= end) {
    return current >= start && current < end;
  } else {
    // 跨午夜
    return current >= start || current < end;
  }
}

// 计算渐变进度 (0.0 ~ 1.0)
static float calcProgress(int16_t current, int16_t start, int16_t end) {
  int16_t duration;
  int16_t elapsed;
  
  if (start <= end) {
    duration = end - start;
    elapsed = current - start;
  } else {
    // 跨午夜
    duration = (1440 - start) + end;
    elapsed = (current >= start) ? (current - start) : (current + 1440 - start);
  }
  
  if (duration <= 0) return 1.0f;
  float progress = (float)elapsed / (float)duration;
  return (progress < 0.0f) ? 0.0f : ((progress > 1.0f) ? 1.0f : progress);
}

// ============ 核心灯效逻辑 ============

void updateTimeEffect(uint8_t& h, uint8_t& s, uint8_t& v) {
  h = teConfig.hue;
  s = teConfig.saturation;
  v = 0;  // 默认关闭
  
  if (!isTimeSynced()) {
    currentPhase = 0;
    return;
  }
  
  int16_t now = getCurrentMinutes();
  if (now < 0) {
    currentPhase = 0;
    return;
  }
  
  int16_t sunrise = getTodaySunrise();
  int16_t sunset = getActualStartTime();
  int16_t peakTime = teConfig.peakTime;
  int16_t nightTime = teConfig.nightTime;
  int16_t offTime = getActualOffTime();
  
  // 调试输出（每分钟输出一次）
  static int16_t lastLogMinute = -1;
  if (now != lastLogMinute) {
    lastLogMinute = now;
    Serial.printf(TR("[TimeEffect] Now:%s Sunrise:%s Sunset:%s Peak:%s Night:%s Off:%s\n",
                  "[TimeEffect] 当前:%s 日出:%s 日落:%s 最亮:%s 夜灯:%s 关闭:%s\n"),
                  minutesToTimeStr(now).c_str(),
                  minutesToTimeStr(sunrise).c_str(),
                  minutesToTimeStr(sunset).c_str(),
                  minutesToTimeStr(peakTime).c_str(),
                  minutesToTimeStr(nightTime).c_str(),
                  minutesToTimeStr(offTime).c_str());
  }
  
  // 判断当前阶段
  // 阶段1: 白天关闭 (日出 ~ 日落)
  if (isTimeInRange(now, sunrise, sunset)) {
    currentPhase = 0;  // 白天关闭
    v = 0;
    return;
  }
  
  // 阶段2: 渐亮期 (日落 ~ 最亮时间)
  if (isTimeInRange(now, sunset, peakTime)) {
    currentPhase = 1;  // 渐亮
    float progress = calcProgress(now, sunset, peakTime);
    v = (uint8_t)(teConfig.maxBrightness * progress);
    return;
  }
  
  // 阶段3: 最亮期 (最亮时间 ~ 夜灯时间)
  if (isTimeInRange(now, peakTime, nightTime)) {
    currentPhase = 2;  // 最亮
    v = teConfig.maxBrightness;
    return;
  }
  
  // 阶段4: 渐暗期 (夜灯时间 ~ 夜灯时间+渐暗时长) 或直接进入夜灯
  int16_t fadeEndTime = nightTime + teConfig.fadeDownDuration;
  if (fadeEndTime >= 1440) fadeEndTime -= 1440;
  
  if (teConfig.fadeDownDuration > 0 && isTimeInRange(now, nightTime, fadeEndTime)) {
    currentPhase = 3;  // 渐暗
    float progress = calcProgress(now, nightTime, fadeEndTime);
    uint8_t diff = teConfig.maxBrightness - teConfig.nightBrightness;
    v = teConfig.maxBrightness - (uint8_t)(diff * progress);
    return;
  }
  
  // 阶段5: 夜灯期 (渐暗结束 ~ 关闭时间)
  int16_t nightLightStart = (teConfig.fadeDownDuration > 0) ? fadeEndTime : nightTime;
  if (isTimeInRange(now, nightLightStart, offTime)) {
    currentPhase = 4;  // 夜灯
    v = teConfig.nightBrightness;
    return;
  }
  
  // 阶段6: 夜间关闭 (关闭时间 ~ 日出)
  currentPhase = 5;  // 夜间关闭
  v = 0;
}

// ============ 灯效函数入口 (effectMode = 1) ============
void fx_timeLight() {
  uint8_t h, s, v;
  updateTimeEffect(h, s, v);
  
  // 应用到所有LED
  CHSV color(h, s, v);
  fill_solid(leds, NUM_LEDS, color);
}

uint8_t getTimeEffectPhase() {
  return currentPhase;
}

const char* getTimeEffectPhaseName() {
  static const char* namesEN[] = {
    "Day Off", "Fading Up", "Peak", "Fading Down", "Night Light", "Night Off"
  };
  static const char* namesZH[] = {
    "白天关闭", "渐亮中", "最亮", "渐暗中", "夜灯", "夜间关闭"
  };
  if (currentPhase > 5) return TR("Unknown", "未知");
  return (getLang() == LANG_ZH) ? namesZH[currentPhase] : namesEN[currentPhase];
}

// ============ BLE 数据序列化 ============
void serializeTimeEffectConfig(uint8_t* buf) {
  buf[0] = teConfig.hue;
  buf[1] = teConfig.saturation;
  buf[2] = teConfig.maxBrightness;
  buf[3] = teConfig.nightBrightness;
  buf[4] = teConfig.startTime & 0xFF;
  buf[5] = (teConfig.startTime >> 8) & 0xFF;
  buf[6] = teConfig.peakTime & 0xFF;
  buf[7] = (teConfig.peakTime >> 8) & 0xFF;
  buf[8] = teConfig.nightTime & 0xFF;
  buf[9] = (teConfig.nightTime >> 8) & 0xFF;
  buf[10] = teConfig.offTime & 0xFF;
  buf[11] = (teConfig.offTime >> 8) & 0xFF;
  buf[12] = teConfig.fadeUpDuration;
  buf[13] = teConfig.fadeDownDuration;
}

bool deserializeTimeEffectConfig(const uint8_t* buf, size_t len) {
  if (len < 14) return false;
  
  teConfig.hue = buf[0];
  teConfig.saturation = buf[1];
  teConfig.maxBrightness = buf[2];
  teConfig.nightBrightness = buf[3];
  teConfig.startTime = (int16_t)(buf[4] | (buf[5] << 8));
  teConfig.peakTime = (int16_t)(buf[6] | (buf[7] << 8));
  teConfig.nightTime = (int16_t)(buf[8] | (buf[9] << 8));
  teConfig.offTime = (int16_t)(buf[10] | (buf[11] << 8));
  teConfig.fadeUpDuration = buf[12];
  teConfig.fadeDownDuration = buf[13];
  
  Serial.printf(TR("[TimeEffect] BLE config update: H=%d S=%d maxV=%d nightV=%d\n",
                "[TimeEffect] BLE配置更新: H=%d S=%d maxV=%d nightV=%d\n"),
                teConfig.hue, teConfig.saturation, teConfig.maxBrightness, teConfig.nightBrightness);
  Serial.printf(TR("[TimeEffect] Times: start=%d peak=%d night=%d off=%d fadeDown=%d\n",
                "[TimeEffect] 时间: start=%d peak=%d night=%d off=%d fadeDown=%d\n"),
                teConfig.startTime, teConfig.peakTime, teConfig.nightTime, teConfig.offTime, teConfig.fadeDownDuration);
  
  return true;
}

void getTimeEffectStatus(uint8_t* buf) {
  uint8_t h, s, v;
  updateTimeEffect(h, s, v);
  
  uint16_t sunrise = getTodaySunrise();
  uint16_t sunset = getTodaySunset();
  
  buf[0] = currentPhase;
  buf[1] = v;  // 当前亮度
  buf[2] = sunrise / 60;  // 日出小时
  buf[3] = sunset / 60;   // 日落小时
}
