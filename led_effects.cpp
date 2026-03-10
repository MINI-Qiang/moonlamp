#include "led_effects.h"
#include "time_effect.h"

// ============ 辅助: 均匀填色 + 微差异增强 ============
// 所有LED基于baseHue施加微量噪声偏移，扩散后光色更有层次
static void fillSphere(uint8_t h, uint8_t s, uint8_t v, uint8_t hueSpread = 0) {
  if (hueSpread == 0) {
    fill_solid(leds, NUM_LEDS, CHSV(h, s, v));
  } else {
    for (int i = 0; i < NUM_LEDS; i++) {
      uint8_t noise = inoise8(i * 50, millis() / 20);
      uint8_t offset = scale8(noise, hueSpread);
      leds[i] = CHSV(h + offset, s, v);
    }
  }
}

// ============ 100: 柔和呼吸 ============
// Speed=呼吸频率(6-60BPM)  Param1=最低亮度(0-200)
void fx_breathe() {
  uint8_t bpm = map(effectSpeed, 0, 255, 6, 60);
  uint8_t minBri = map(effectParam1, 0, 255, 0, 200);
  uint8_t raw = beatsin8(bpm, 0, 255);
  uint8_t bri = ease8InOutCubic(raw);
  bri = map(bri, 0, 255, minBri, 255);
  fillSphere(currentH, currentS, bri);
}

// ============ 101: 色相漫游 ============
// Speed=漫游速度  Param1=饱和度
void fx_hueNoise() {
  uint16_t scale = map(effectSpeed, 0, 255, 80, 5);
  uint8_t hue = inoise8(millis() / scale);
  uint8_t sat = effectParam1;
  fillSphere(hue, sat, 220, 16);
}

// ============ 102: 调色板呼吸 ============
// Speed=色彩流转速度  Param1=调色板选择(6段)  Param2=呼吸深度
void fx_paletteBreathe() {
  static CRGBPalette16 pal = OceanColors_p;
  static uint8_t lastPalSel = 255;
  uint8_t palSel = effectParam1 / 43;
  if (palSel != lastPalSel) {
    lastPalSel = palSel;
    switch (palSel) {
      case 0: pal = RainbowColors_p; break;
      case 1: pal = OceanColors_p; break;
      case 2: pal = LavaColors_p; break;
      case 3: pal = ForestColors_p; break;
      case 4: pal = PartyColors_p; break;
      default: pal = CloudColors_p; break;
    }
  }
  static uint16_t idx = 0;
  idx += map(effectSpeed, 0, 255, 1, 10);
  uint8_t depth = map(effectParam2, 0, 255, 30, 200);
  uint8_t bri = beatsin8(15, 255 - depth, 255);
  CRGB c = ColorFromPalette(pal, idx >> 4, bri, LINEARBLEND);
  fill_solid(leds, NUM_LEDS, c);
}

// ============ 103: 烛光摇曳 ============
// Speed=摇曳频率  Param1=色温(暖黄→暖白)  Param2=摇曳幅度
void fx_candle() {
  uint16_t t = millis();
  uint8_t speedMul = map(effectSpeed, 0, 255, 1, 4);
  uint8_t amplitude = map(effectParam2, 0, 255, 40, 180);

  // 三层噪声叠加: 慢呼吸 + 中频抖动 + 快速闪烁
  uint8_t slow  = inoise8(t / 6 * speedMul, 1000);       // 慢速起伏
  uint8_t mid   = inoise8(t / 2 * speedMul, 5000);       // 中频抖动
  uint8_t fast  = inoise8(t * speedMul, 9000);            // 快速闪烁

  // 加权混合: 慢40% + 中35% + 快25%
  uint16_t mixed = ((uint16_t)slow * 102 + (uint16_t)mid * 89 + (uint16_t)fast * 64) >> 8;

  // 偶发骤降: ~6%概率出现明显暗闪 (模拟风吹)
  uint8_t gustNoise = inoise8(t * 3, 30000);
  if (gustNoise < 16) {
    mixed = mixed * (uint16_t)(gustNoise + 4) / 20;  // 骤降到20%~100%
  }

  uint8_t minBri = 255 - amplitude;
  uint8_t bri = map(constrain(mixed, 0, 255), 0, 255, minBri, 255);

  // 色温: param1=0→暖黄(hue32,sat255), param1=255→暖白(hue40,sat100)
  uint8_t hue = map(effectParam1, 0, 255, 32, 40);
  uint8_t sat = map(effectParam1, 0, 255, 255, 100);
  // 暗淡时偏更暖 (白炽灯特性)
  if (bri < 160) {
    sat = qadd8(sat, scale8(160 - bri, 60));
    hue = qsub8(hue, scale8(160 - bri, 8));
  }
  fillSphere(hue, sat, bri, 4);
}

// ============ 104: 潮汐渐变 ============
// Speed=潮汐周期  Param1=色相范围  Param2=亮度波动幅度
void fx_tidal() {
  uint8_t period = map(effectSpeed, 0, 255, 5, 40);
  uint8_t hueRange = map(effectParam1, 0, 255, 20, 128);
  uint8_t briAmplitude = map(effectParam2, 0, 255, 30, 120);
  // 双速正弦叠加: 慢周期主色相 + 快周期微调
  uint8_t hue1 = beatsin8(period, currentH, currentH + hueRange);
  uint8_t hue2 = beatsin8(period * 3, 0, hueRange / 4, 0, 64);
  uint8_t bri = beatsin8(period * 2, 255 - briAmplitude, 255, 0, 128);
  fillSphere(hue1 + hue2, 230, bri, 8);
}

// ============ 105: 日落渐变 ============
// Speed=流转速度  暖色调色板慢速漫游
void fx_sunset() {
  static CRGBPalette16 sunsetPal;
  static bool palInited = false;
  if (!palInited) {
    palInited = true;
    // 日落渐变: 深红→橘红→金黄→玫红→紫红
    sunsetPal = CRGBPalette16(
      CHSV(0, 255, 180), CHSV(16, 255, 220),
      CHSV(28, 255, 255), CHSV(36, 240, 255),
      CHSV(28, 255, 255), CHSV(16, 255, 220),
      CHSV(248, 220, 200), CHSV(224, 200, 160),
      CHSV(0, 255, 180), CHSV(16, 255, 220),
      CHSV(28, 255, 255), CHSV(36, 240, 255),
      CHSV(28, 255, 255), CHSV(16, 255, 220),
      CHSV(248, 220, 200), CHSV(224, 200, 160)
    );
  }
  static uint16_t idx = 0;
  idx += map(effectSpeed, 0, 255, 1, 8);
  CRGB c = ColorFromPalette(sunsetPal, idx >> 4, 255, LINEARBLEND);
  fill_solid(leds, NUM_LEDS, c);
}

// ============ 106: 极光流转 ============
// Speed=演变速度  Param1=饱和度范围  Param2=亮度深度
void fx_aurora() {
  static uint16_t sHue16 = 0;
  static uint16_t sLastMs = 0;
  uint16_t ms = millis();
  uint16_t delta = ms - sLastMs;
  sLastMs = ms;
  uint8_t speedMul = map(effectSpeed, 0, 255, 10, 60);
  sHue16 += delta * beatsin88(400, 3, speedMul);
  uint8_t hue = sHue16 >> 8;
  uint8_t satLo = map(effectParam1, 0, 255, 180, 240);
  uint8_t satHi = map(effectParam1, 0, 255, 230, 255);
  uint8_t sat = beatsin88(87, satLo, satHi);
  uint8_t depthLo = map(effectParam2, 0, 255, 160, 60);
  uint8_t bri8 = beatsin88(341, depthLo, 255);
  fillSphere(hue, sat, bri8, 12);
}

// ============ 107: 心跳脉冲 ============
// Speed=心率(40-120BPM)  Param1=峰值亮度
void fx_heartbeat() {
  uint8_t bpm = map(effectSpeed, 0, 255, 40, 120);
  uint8_t peakBri = map(effectParam1, 0, 255, 160, 255);
  uint8_t phase = beat8(bpm);
  uint8_t bri = 0;
  // 双峰波: 第一峰(0-50) → 间歇(50-70) → 第二峰(70-110) → 长间歇
  if (phase < 50) {
    uint8_t t = phase * 5; // 0→250
    bri = ease8InOutCubic(t);
  } else if (phase < 70) {
    uint8_t t = (70 - phase) * 12; // 240→0
    bri = scale8(ease8InOutCubic(t), 180);
  } else if (phase < 110) {
    uint8_t t = (phase - 70) * 6; // 0→240
    bri = scale8(ease8InOutCubic(t), 200);
  } else if (phase < 140) {
    uint8_t t = (140 - phase) * 8; // 240→0
    bri = scale8(ease8InOutCubic(t), 140);
  }
  bri = scale8(bri, peakBri);
  fillSphere(currentH, currentS, bri);
}

// ============ 108: 月光涟漪 ============
// Speed=变化速度  Param1=色相偏移基准
void fx_moonlight() {
  uint16_t scale = map(effectSpeed, 0, 255, 60, 8);
  uint16_t t = millis() / scale;
  uint8_t noiseHue = inoise8(t);
  uint8_t hue = effectParam1 + (noiseHue >> 2); // 基准色相 + Noise/4
  uint8_t raw = cubicwave8(t & 0xFF);
  uint8_t bri = map(raw, 0, 255, 120, 255);
  fillSphere(hue, 180, bri, 10);
}

// ============ 109: 色彩交融 ============
// Speed=切换速度  Param1=起始调色板  Param2=目标调色板
void fx_colorBlend() {
  static CRGBPalette16 curPal = OceanColors_p;
  static CRGBPalette16 tgtPal = LavaColors_p;
  static uint8_t lastP1 = 255, lastP2 = 255;
  // 根据参数选择调色板
  auto selectPal = [](uint8_t sel) -> CRGBPalette16 {
    switch (sel / 43) {
      case 0: return RainbowColors_p;
      case 1: return OceanColors_p;
      case 2: return LavaColors_p;
      case 3: return ForestColors_p;
      case 4: return PartyColors_p;
      default: return CloudColors_p;
    }
  };
  if (effectParam1 != lastP1) { lastP1 = effectParam1; curPal = selectPal(effectParam1); }
  if (effectParam2 != lastP2) { lastP2 = effectParam2; tgtPal = selectPal(effectParam2); }
  uint8_t blendRate = map(effectSpeed, 0, 255, 2, 24);
  EVERY_N_MILLISECONDS(20) {
    nblendPaletteTowardPalette(curPal, tgtPal, blendRate);
  }
  static uint16_t idx = 0;
  idx += 3;
  CRGB c = ColorFromPalette(curPal, idx >> 4, 255, LINEARBLEND);
  fill_solid(leds, NUM_LEDS, c);
}

// ============ 110: 萤火虫 ============
// Speed=闪烁频率  Param1=色彩范围(暖色偏移)
void fx_firefly() {
  static uint8_t brightness = 0;
  static uint8_t target = 0;
  static unsigned long lastChange = 0;
  uint16_t interval = map(effectSpeed, 0, 255, 3000, 300);
  unsigned long now = millis();
  if (now - lastChange >= interval) {
    lastChange = now;
    // 随机决定亮或暗
    if (target == 0) {
      target = random8(180, 255);
    } else {
      target = 0;
    }
  }
  // 平滑过渡
  if (brightness < target) brightness = qadd8(brightness, 3);
  else if (brightness > target) brightness = qsub8(brightness, 3);
  uint8_t hueRange = map(effectParam1, 0, 255, 0, 28);
  // 用慢速噪声替代random8，避免每帧色相剧烈跳变
  uint8_t hueNoise = inoise8(millis() / 40, 9999);
  uint8_t hue = 36 - scale8(hueNoise, hueRange); // 暖黄基准 - 平滑偏移(向橙色方向)
  if (brightness > 20) {
    fillSphere(hue, 200, brightness, 6);
  } else {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
  }
}

// ============ 111: 四季流转 ============
// Speed=季节流转速度  自动在4种调色板间轮换
void fx_seasons() {
  // 春: 粉绿  夏: 蓝绿  秋: 橙红  冬: 蓝白
  static CRGBPalette16 springPal, summerPal, autumnPal, winterPal;
  static CRGBPalette16 curPal;
  static bool inited = false;
  if (!inited) {
    inited = true;
    springPal = CRGBPalette16(CHSV(96,200,255), CHSV(80,180,240), CHSV(224,160,240), CHSV(96,220,200),
                               CHSV(80,200,255), CHSV(96,180,240), CHSV(224,160,240), CHSV(80,220,200),
                               CHSV(96,200,255), CHSV(80,180,240), CHSV(224,160,240), CHSV(96,220,200),
                               CHSV(80,200,255), CHSV(96,180,240), CHSV(224,160,240), CHSV(80,220,200));
    summerPal = CRGBPalette16(CHSV(128,255,255), CHSV(140,240,240), CHSV(96,255,220), CHSV(160,200,255),
                               CHSV(128,255,255), CHSV(140,240,240), CHSV(96,255,220), CHSV(160,200,255),
                               CHSV(128,255,255), CHSV(140,240,240), CHSV(96,255,220), CHSV(160,200,255),
                               CHSV(128,255,255), CHSV(140,240,240), CHSV(96,255,220), CHSV(160,200,255));
    autumnPal = CRGBPalette16(CHSV(16,255,255), CHSV(24,240,240), CHSV(0,255,200), CHSV(32,255,255),
                               CHSV(16,255,255), CHSV(24,240,240), CHSV(0,255,200), CHSV(32,255,255),
                               CHSV(16,255,255), CHSV(24,240,240), CHSV(0,255,200), CHSV(32,255,255),
                               CHSV(16,255,255), CHSV(24,240,240), CHSV(0,255,200), CHSV(32,255,255));
    winterPal = CRGBPalette16(CHSV(160,80,255), CHSV(170,60,240), CHSV(140,40,200), CHSV(160,100,180),
                               CHSV(160,80,255), CHSV(170,60,240), CHSV(140,40,200), CHSV(160,100,180),
                               CHSV(160,80,255), CHSV(170,60,240), CHSV(140,40,200), CHSV(160,100,180),
                               CHSV(160,80,255), CHSV(170,60,240), CHSV(140,40,200), CHSV(160,100,180));
    curPal = springPal;
  }
  // 循环: 春→夏→秋→冬 (基于累积时间，调速即时生效)
  static unsigned long lastMs = 0;
  static unsigned long accumMs = 0;
  unsigned long nowMs = millis();
  unsigned long deltaMs = nowMs - lastMs;
  lastMs = nowMs;
  if (deltaMs > 200) deltaMs = 200; // 防溢出
  // speed 0→255 映射为每季 120s→5s
  uint16_t cycleMs = map(effectSpeed, 0, 255, 120000, 5000);
  accumMs += deltaMs;
  if (accumMs >= cycleMs * 4UL) accumMs %= (cycleMs * 4UL);
  uint8_t season = accumMs / cycleMs;
  CRGBPalette16 tgt;
  switch (season) {
    case 0: tgt = springPal; break;
    case 1: tgt = summerPal; break;
    case 2: tgt = autumnPal; break;
    default: tgt = winterPal; break;
  }
  EVERY_N_MILLISECONDS(20) {
    nblendPaletteTowardPalette(curPal, tgt, 8);
  }
  static uint16_t idx = 0;
  idx += 2;
  uint8_t bri = beatsin8(10, 200, 255);
  CRGB c = ColorFromPalette(curPal, idx >> 4, bri, LINEARBLEND);
  fill_solid(leds, NUM_LEDS, c);
}

// ============ 灯效调度 ============
void updateEffect() {
  if (!powerOn) {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    return;
  }
  switch (effectMode) {
    // 时间智能灯效
    case 1: fx_timeLight(); break;
    // 球形灯灯效 100+
    case 100: fx_breathe(); break;
    case 101: fx_hueNoise(); break;
    case 102: fx_paletteBreathe(); break;
    case 103: fx_candle(); break;
    case 104: fx_tidal(); break;
    case 105: fx_sunset(); break;
    case 106: fx_aurora(); break;
    case 107: fx_heartbeat(); break;
    case 108: fx_moonlight(); break;
    case 109: fx_colorBlend(); break;
    case 110: fx_firefly(); break;
    case 111: fx_seasons(); break;
    default: return;
  }
  FastLED.show();
}

// ============ LED更新 (静态模式) ============
void applyLED() {
  if (effectMode != 0) return;
  if (powerOn) {
    CHSV color(currentH, currentS, currentV);
    fill_solid(leds, NUM_LEDS, color);
  } else {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
  }
  FastLED.show();
}
