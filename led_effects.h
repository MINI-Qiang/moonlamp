#ifndef LED_EFFECTS_H
#define LED_EFFECTS_H

#include "config.h"
#include "time_effect.h"

// 球形灯灯效 (均匀混色，时间维度变化)
void fx_breathe();       // 100: 柔和呼吸
void fx_hueNoise();      // 101: 色相漫游
void fx_paletteBreathe(); // 102: 调色板呼吸
void fx_candle();        // 103: 烛光摇曳
void fx_tidal();         // 104: 潮汐渐变
void fx_sunset();        // 105: 日落渐变
void fx_aurora();        // 106: 极光流转
void fx_heartbeat();     // 107: 心跳脉冲
void fx_moonlight();     // 108: 月光涟漪
void fx_colorBlend();    // 109: 色彩交融
void fx_firefly();       // 110: 萤火虫
void fx_seasons();       // 111: 四季流转

// 灯效调度与静态模式
void updateEffect();
void applyLED();

#endif
