#pragma once
// 谷仓次元屏关机（APP_SDK：app 内应回 launcher，真断电由官方电源管理负责）。
// 由 MotoNav.ino 按依赖顺序包含，仅编译一次。
#include "esp_ota_ops.h"
#include "esp_partition.h"

// 官方 sdgoods_power_off() 语义：
//   关背光/关屏 -> 设启动槽回 factory(launcher) -> 释放电池自锁闩 GPIO7(拉低)
//   -> 若板级没真断电(USB 仍供电)，兜底进深睡，电源键 GPIO6 低电平唤醒。
static void sdgoodsPowerDown(bool backToLauncher) {
  powerOffPending = true;
  Serial.println("[pwr] SDGOODS power down");
  const bool displayReady = !flushDone ||
      xSemaphoreTake(flushDone, pdMS_TO_TICKS(3000)) == pdTRUE;
  if (displayReady) {
    boardBrightness(0);
    panel->displayOff();
  }
  gpio_set_level((gpio_num_t)PA, 0);            // 关功放，避免掉电爆音

  if (backToLauncher) {
    const esp_partition_t *factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
    if (factory) esp_ota_set_boot_partition(factory);
    Serial.println("[pwr] boot -> launcher");
  }

  // 停蓝牙主机/控制器，但不删除仍可能被回调引用的 GATT 对象（深睡会丢 RAM）。
#if defined(CONFIG_NIMBLE_ENABLED)
  nimble_port_stop();
#elif defined(CONFIG_BLUEDROID_ENABLED)
  esp_bluedroid_disable();
#endif
#if CONFIG_BT_CONTROLLER_ENABLED
  esp_bt_controller_disable();
#endif

  gpio_set_level((gpio_num_t)BOARD_BAT_LATCH_PIN, 0);   // 释放自锁闩 = 硬断电
  delay(200);

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  // 等电源键松开，避免按着键进深睡被 EXT0 立刻唤醒
  uint32_t offWait = millis();
  while (millis() - offWait < 8000 &&
         digitalRead(BOARD_POWER_KEY_PIN) == LOW) delay(20);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BOARD_POWER_KEY_PIN, 0);
  Serial.println("[pwr] latch released; deep sleep fallback");
  esp_deep_sleep_start();
}

void idlePowerOffShutdown() {
  // 本板无 VBUS 检测脚，idlePowerSupply 恒为 unknown => 这条路径正常不会触发。
  sdgoodsPowerDown(true);
}

// app 内主动关机（串口 / App 命令可调用）：同样先指回 launcher，下次开机进菜单。
void devicePowerOff() {
  sdgoodsPowerDown(true);
}
