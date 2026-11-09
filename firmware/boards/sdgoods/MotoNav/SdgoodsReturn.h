#pragma once
// 谷仓 SDGOODS 多应用模式：app 是完整固件，bootloader 直接进 ota_N，
// 重启后【不会】自动回 launcher。没有返回入口 = 用户进 app 后出不来，
// 这是官方 APP_SDK.md 明确的上架否决项。
//
// 官方 SDK 的 sdgoods_return_to_launcher() 只是下面这层 IDF API 的薄封装
// （components/sdgoods_launcher/src/app_sdk.c -> launch_slot(-1)）。
// Arduino 固件不链接该组件，直接调 IDF 即可，效果完全一致。
#include <Arduino.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"

#define SDG_POWER_KEY_PIN       6      // 官方 board_pins.h：电源键，低有效
#define SDG_RETURN_HOLD_MS      3000   // 长按 3 秒回启动器

inline bool sdgoodsReturnToLauncher() {
  // 单应用直启（本固件就跑在 factory 里）时，factory 不是 launcher，
  // 设回 factory 只会重启回自己 —— 直接拒绝，避免长按后“莫名重启”。
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running && running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
    Serial.println("[sdg] no launcher installed (running from factory), return disabled");
    return false;
  }
  const esp_partition_t *factory = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
  if (!factory) {
    Serial.println("[sdg] factory partition not found");
    return false;
  }
  esp_err_t err = esp_ota_set_boot_partition(factory);
  if (err != ESP_OK) {
    Serial.printf("[sdg] set_boot_partition(factory) failed: %d\n", (int)err);
    return false;
  }
  Serial.println("[sdg] boot -> launcher, restarting");
  esp_restart();
  return true;                       // 不会走到这里
}

// 在 loop() 里每轮调用。两种返回方式：电源键长按 3s / 串口输入大写 L。
// v7.28：电源键统一交给 MotoNav.ino 的 sdgoodsPowerKeyPoll() 处理（长按开关机 /
// 待机切换 / 导航切地图）。这里不再读取 GPIO6，避免同一按键被两套逻辑重复处理；
// 只保留「串口 L -> 返回 launcher」，供将来装上官方 launcher 时使用。
inline void sdgoodsReturnPoll() {
  static bool inited = false;
  static bool noLauncher = false;
  if (!inited) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    noLauncher = running && running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY;
    if (noLauncher)
      Serial.println("[sdg] single-app factory boot: serial-L return disabled");
    inited = true;
  }
  // 用 peek 只取走 'L'，其余字符留给 loop() 的串口自检等其它命令
  if (!noLauncher && Serial.available() && Serial.peek() == 'L') {
    Serial.read();
    sdgoodsReturnToLauncher();
  }
}
