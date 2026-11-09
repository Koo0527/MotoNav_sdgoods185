#pragma once
// 谷仓次元屏显示/电源板级层：ST77916 QSPI 面板类 + 上电自锁 + 背光 + 亮度接口。
// 依赖顺序：DeviceContext.h 先包含 Arduino_GFX_Library.h 与 pin_config.h，再包含本文件。
#include <Arduino.h>
#include "driver/gpio.h"
#include "SdgoodsPanelInit.h"

static bool backlightPWM = false;

// 官方 main.c：开机第一件事就是把电池自锁闩 GPIO7 拉高，否则 USB 一断开就掉电。
// 同时配置电源键 GPIO6（低有效，上拉输入）、屏电源 GPIO12（低有效）、功放 GPIO3（初始关）。
inline void boardPowerInit() {
  gpio_config_t latch = {
    .pin_bit_mask = 1ULL << BOARD_BAT_LATCH_PIN,
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&latch);
  gpio_set_level((gpio_num_t)BOARD_BAT_LATCH_PIN, 1);

  gpio_config_t key = {
    .pin_bit_mask = 1ULL << BOARD_POWER_KEY_PIN,
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&key);

  gpio_config_t pa = {
    .pin_bit_mask = 1ULL << PA,
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&pa);
  gpio_set_level((gpio_num_t)PA, 0);
}

// 官方 sdgoods_lcd.c：背光 GPIO13 用 LEDC（非反相）；Arduino 侧用 8bit / 20kHz。
inline bool boardBrightnessInit() {
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, LOW);
  backlightPWM = ledcAttach(LCD_BL, 20000, 8);
  Serial.printf("[lcd] backlight PWM=%d\n", (int)backlightPWM);
  return backlightPWM;
}
inline void boardBrightness(uint8_t value) {
  if (backlightPWM) ledcWrite(LCD_BL, value);
  else digitalWrite(LCD_BL, value > 0 ? HIGH : LOW);
}

// MADCTL 0x60 是谷仓官方 BSP 的权威值；Arduino_GFX 的 setRotation 默认写 0x00，
// 这里覆盖后无论 begin() 还是后续旋转调用都保持官方扫描方向，
// 画面朝向的旋转统一交给 PortScale（逻辑画布层）处理。
class BoardSdgoodsPanel : public Arduino_ST77916 {
public:
  BoardSdgoodsPanel(Arduino_DataBus *bus)
    : Arduino_ST77916(bus, LCD_RESET, 0, true, PANEL_WIDTH, PANEL_HEIGHT) {}

  void setRotation(uint8_t r) override {
    Arduino_TFT::setRotation(r);            // 更新宽高与起始偏移
    _bus->beginWrite();
    _bus->writeC8D8(ST77916_MADCTL, ST77916_MADCTL_VALUE);
    _bus->endWrite();
  }

protected:
  void tftInit() override {
    // 官方 lcd_pow_on()：GPIO12 拉低 10ms 给屏控制器供电
    gpio_config_t pow = {
      .pin_bit_mask = 1ULL << LCD_POW_EN,
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pow);
    gpio_set_level((gpio_num_t)LCD_POW_EN, 0);
    delay(10);

    // 官方 lcd_hw_reset()：GPIO9 拉低 10ms，拉高后等 50ms
    gpio_config_t rst = {
      .pin_bit_mask = 1ULL << LCD_RESET,
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&rst);
    gpio_set_level((gpio_num_t)LCD_RESET, 0);
    delay(10);
    gpio_set_level((gpio_num_t)LCD_RESET, 1);
    delay(50);

    _bus->batchOperation(sdgoods_panel_init, sizeof(sdgoods_panel_init));
    // 不调用基类 invertDisplay(false)：官方 BSP 保持 init 里的 0x21(INVON)。
    Serial.println("[lcd] ST77916 vendor init done, 360x360 QSPI");
  }
};
