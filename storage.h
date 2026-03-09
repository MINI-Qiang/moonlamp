#ifndef STORAGE_H
#define STORAGE_H

#include "config.h"

// 初始化NVS并加载设置
void initStorage();

// 从NVS加载设置到全局变量
void loadSettings();

// 如果值有变化，保存到NVS
void saveSettingsIfChanged();

#endif
