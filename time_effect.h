#ifndef TIME_EFFECT_H
#define TIME_EFFECT_H

#include "config.h"

// ============ 时间灯效系统 (effectMode = 1) ============
// 基于日出日落的智能灯光控制
//
// 时间线 (示例):
// ─────────────────────────────────────────────────────────────
// 日出(06:00)                日落(18:00)  最亮(21:00)  夜灯(21:30)  关闭(05:30)
//   ↓                           ↓            ↓           ↓           ↓
// [关闭]────────────────────[渐亮]────────[最亮]────[渐暗]─[夜灯]─[关闭]
//
// 阶段:
// 1. 白天关闭: 日出 ~ 日落
// 2. 渐亮期: 日落 ~ 最亮时间 (亮度从0渐变到最大)
// 3. 最亮期: 维持最大亮度
// 4. 渐暗期: 最亮时间 ~ 夜灯时间 (亮度渐变到夜灯亮度)
// 5. 夜灯期: 维持夜灯亮度
// 6. 夜间关闭: 关闭时间 ~ 日出

// ============ 时间灯效配置结构 ============
struct TimeEffectConfig {
  // 颜色参数 (HSV)
  uint8_t hue;           // 色相 0-255 (默认206 = 291°)
  uint8_t saturation;    // 饱和度 0-255 (默认0)
  
  // 亮度参数
  uint8_t maxBrightness; // 最大亮度 0-255 (默认255)
  uint8_t nightBrightness; // 夜灯亮度 0-255 (默认30)
  
  // 时间参数 (分钟数 0-1439, -1表示使用日出日落计算)
  int16_t startTime;     // 开始渐亮时间 (-1=日落)
  int16_t peakTime;      // 最亮时间 (默认21:00 = 1260)
  int16_t nightTime;     // 进入夜灯时间 (默认21:30 = 1290)
  int16_t offTime;       // 关闭时间 (-1=日出前30分钟)
  
  // 渐变时长（分钟）
  uint8_t fadeUpDuration;   // 渐亮时长 (0=使用startTime到peakTime)
  uint8_t fadeDownDuration; // 渐暗时长 (默认30分钟)
};

// ============ 函数声明 ============

// 初始化时间灯效系统（从NVS加载配置）
void initTimeEffect();

// 获取/设置时间灯效配置
TimeEffectConfig& getTimeEffectConfig();
void setTimeEffectConfig(const TimeEffectConfig& cfg);

// 保存配置到NVS
void saveTimeEffectConfig();

// 加载配置从NVS
void loadTimeEffectConfig();

// 更新时间灯效（在loop中调用）
// 返回当前计算的HSV值
void updateTimeEffect(uint8_t& h, uint8_t& s, uint8_t& v);

// 时间灯效主函数（灯效ID=1）
void fx_timeLight();

// 获取当前灯效阶段（调试用）
// 0=白天关闭, 1=渐亮, 2=最亮, 3=渐暗, 4=夜灯, 5=夜间关闭
uint8_t getTimeEffectPhase();

// 获取阶段名称
const char* getTimeEffectPhaseName();

// ============ BLE 数据序列化 ============
// 将配置序列化为14字节数据（用于BLE传输）
// 格式: [hue][sat][maxV][nightV][startT_L][startT_H][peakT_L][peakT_H]
//       [nightT_L][nightT_H][offT_L][offT_H][fadeUp][fadeDown]
void serializeTimeEffectConfig(uint8_t* buf);

// 从14字节数据反序列化配置
bool deserializeTimeEffectConfig(const uint8_t* buf, size_t len);

// 获取当前状态信息（用于BLE读取，4字节）
// 格式: [phase][currentV][sunriseH][sunsetH]
void getTimeEffectStatus(uint8_t* buf);

#endif
