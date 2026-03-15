#include "storage.h"
#include "i18n.h"
#include <Preferences.h>

static Preferences prefs;

// NVS中已存储的值(用于比对)
static uint8_t savedH = 0;
static uint8_t savedS = 255;
static uint8_t savedV = 128;
static bool    savedPower = true;
static uint8_t savedEffectMode = 0;
static uint8_t savedEffectSpeed = 128;
static uint8_t savedParam1 = 128;
static uint8_t savedParam2 = 128;

void initStorage() {
  prefs.begin("led_cfg", false);
}

void loadSettings() {
  currentH = prefs.getUChar("hue", 0);
  currentS = prefs.getUChar("sat", 255);
  currentV = prefs.getUChar("val", 128);
  powerOn  = prefs.getBool("power", true);
  effectMode = prefs.getUChar("fxMode", 0);
  effectSpeed = prefs.getUChar("fxSpeed", 128);
  effectParam1 = prefs.getUChar("fxP1", 128);
  effectParam2 = prefs.getUChar("fxP2", 128);
  savedH = currentH; savedS = currentS; savedV = currentV; savedPower = powerOn;
  savedEffectMode = effectMode; savedEffectSpeed = effectSpeed;
  savedParam1 = effectParam1; savedParam2 = effectParam2;
  Serial.printf(TR("[NVS] Loaded: H=%d S=%d V=%d Power=%d FX=%d Speed=%d P1=%d P2=%d\n",
                "[NVS] 已加载: H=%d S=%d V=%d Power=%d FX=%d Speed=%d P1=%d P2=%d\n"),
                currentH, currentS, currentV, powerOn, effectMode, effectSpeed, effectParam1, effectParam2);
}

void saveSettingsIfChanged() {
  if (currentH == savedH && currentS == savedS && currentV == savedV && powerOn == savedPower
      && effectMode == savedEffectMode && effectSpeed == savedEffectSpeed
      && effectParam1 == savedParam1 && effectParam2 == savedParam2) {
    return;
  }
  prefs.putUChar("hue", currentH);
  prefs.putUChar("sat", currentS);
  prefs.putUChar("val", currentV);
  prefs.putBool("power", powerOn);
  prefs.putUChar("fxMode", effectMode);
  prefs.putUChar("fxSpeed", effectSpeed);
  prefs.putUChar("fxP1", effectParam1);
  prefs.putUChar("fxP2", effectParam2);
  savedH = currentH; savedS = currentS; savedV = currentV; savedPower = powerOn;
  savedEffectMode = effectMode; savedEffectSpeed = effectSpeed;
  savedParam1 = effectParam1; savedParam2 = effectParam2;
  Serial.printf(TR("[NVS] Saved: H=%d S=%d V=%d Power=%d FX=%d Speed=%d P1=%d P2=%d\n",
                "[NVS] 设置已保存: H=%d S=%d V=%d Power=%d FX=%d Speed=%d P1=%d P2=%d\n"),
                currentH, currentS, currentV, powerOn, effectMode, effectSpeed, effectParam1, effectParam2);
}
