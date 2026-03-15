#ifndef I18N_H
#define I18N_H

/**
 * 轻量国际化支持
 * 
 * 默认英文输出，可通过串口命令切换为中文。
 * 语言偏好存储在 NVS (config_manager) 中，重启后保持。
 * 
 * 用法: Serial.println(TR("[LED] Power on", "[LED] 已开灯"));
 */

#include <Arduino.h>

enum Lang { LANG_EN = 0, LANG_ZH = 1 };

Lang getLang();
void setLang(Lang lang);

// 根据当前语言选择字符串
#define TR(en, zh) (getLang() == LANG_ZH ? (zh) : (en))

#endif
