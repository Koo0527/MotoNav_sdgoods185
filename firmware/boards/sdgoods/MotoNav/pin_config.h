#pragma once

// 谷仓共创计划 · 谷仓次元屏 / SDGOODS-ESP32S3（电子徽章）
// 权威引脚来源：官方 BSP components/sdgoods_board/include/board_pins.h (Apache-2.0)
// 实测硬件：ESP32-S3-R8 / 8MB Octal PSRAM / 32MB Flash / USB-Serial-JTAG COM7
//
// 与 143C 的关键差异（直接烧 143C 固件必然黑屏）：
//   CO5300 466x466 AMOLED -> ST77916 360x360 QSPI；QSPI 引脚整组不同；
//   CST820@0x15 (I2C 47/48) -> CST816@0x15 (I2C 11/10)；
//   BOOT GPIO0 -> 电源键 GPIO6；电池 GPIO4 分压 -> GPIO8 (1/3 分压) + GPIO7 自锁闩；
//   ES8311 codec -> I2S 直推 Class-D 功放；8MB -> 32MB Flash。
// 因此逻辑画布继续用 466x466，刷屏时统一缩放到 360x360 物理屏（PortScale.h）。

#define BOARD_SDGOODS 1

// ---- 显示屏 ST77916, QSPI, RGB565, 360x360 ----
#define LCD_SDIO0 46
#define LCD_SDIO1 45
#define LCD_SDIO2 42
#define LCD_SDIO3 41
#define LCD_SCLK 40
#define LCD_CS 21
#define LCD_RESET 9
#define LCD_BL 13
#define LCD_TE 18
#define LCD_POW_EN 12            // 官方 lcd_pow_on()：拉低 10ms 给屏控制器上电
#define PANEL_WIDTH 360
#define PANEL_HEIGHT 360
#define ST77916_MADCTL_VALUE 0x60u   // BOARD_ST77916_VENDOR_MADCTL (MX|MV, RGB)
#define ST77916_QSPI_RAMWR_ADDR 0x002C00u  // 0x32 + 0x2C00 (RAMWR)，区别于 CO5300 的 0x3C00

// 逻辑画布：完整保留 466 布局，由 PortScale 缩放到 360 物理面板
#define LCD_WIDTH 466
#define LCD_HEIGHT 466

// ---- 触摸 CST816（与 IMU QMI8658 共用 I2C）----
#define IIC_SDA 11
#define IIC_SCL 10
#define TP_INT 4
#define TP_RESET 5
#define TOUCH_I2C_ADDR 0x15
#define BOARD_TOUCH_FLIP_X 0
#define BOARD_TOUCH_FLIP_Y 0

// ---- 电源 / 按键 ----
#define BOARD_BAT_LATCH_PIN 7    // 电池自锁闩：开机必须拉高，拉低即硬断电
#define BOARD_BAT_ADC_PIN 8      // 电池电压 ADC1_CH7，板载 1/3 分压
#define BOARD_BAT_DIVIDER 3.0f
#define BOARD_POWER_KEY_PIN 6    // 唯一按键（电源键），低有效
// v7.28 单键语义：长按 3s 开关机；待机单击切换待机动画/屏保；导航与漫游单击循环切换地图。
#define SDG_KEY_HOLD_MS      3000   // 长按阈值：开机态关机 / 关机态开机
#define SDG_KEY_TAP_MIN_MS     50   // 短于此按下时长视为抖动，忽略

// ---- 喇叭 I2S（无 ES8311，直推 Class-D 功放）----
#define I2S_MCK_IO 15
#define I2S_BCK_IO 48
#define I2S_WS_IO 38
#define I2S_DO_IO 47
#define I2S_DI_IO (-1)
#define PA 3                     // 功放使能，高有效

#define MCLKPIN 15
#define BCLKPIN 48
#define WSPIN 38
#define DOPIN 47
#define DIPIN (-1)
