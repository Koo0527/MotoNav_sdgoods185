#pragma once
// ============================================================================
// 谷仓次元屏 / 谷仓电子徽章 (SDGOODS-ESP32S3) 官方引脚定义
// 来源：https://github.com/SDGOODS/SDGOODS-ESP32S3
//       components/sdgoods_board/include/board_pins.h  (Apache-2.0)
// 获取日期：2026-11-09    该 BSP 层以 Apache-2.0 发布，可商用、可修改。
//
// 实测硬件（COM7, esptool v5.3.0）：
//   ESP32-S3 QFN56 rev0.2 / 8MB Octal PSRAM / 32MB QSPI Flash (GD c8 4019)
//   MAC ec:da:3b:67:12:0c / USB-Serial-JTAG (VID_303A:1001)
//   出厂固件：MotoNav v7.12.10-pages.21，ESP-IDF v5.5.1 + NimBLE
//   分区：launcher @0x10000 (3MB) + 16 个 OTA 槽 + appdata @0x1000000 (16MB)
//
// 硬件规格（README）：360x360 圆形 ST77916 QSPI 屏、CST816 触摸(I2C)、
//   QMI8658 六轴 IMU、Class-D 功放+喇叭(I2S)、数字麦克风(I2S)、
//   电源键(GPIO6)、500mAh 电池+供电管理(GPIO7)。
// ============================================================================

// ---- 显示屏 ST77916, QSPI, RGB565, 360x360, 80MHz ----
#define BOARD_LCD_SPI_IO_TE              18
#define BOARD_LCD_SPI_IO_SCK             40
#define BOARD_LCD_SPI_IO_DATA0           46
#define BOARD_LCD_SPI_IO_DATA1           45
#define BOARD_LCD_SPI_IO_DATA2           42
#define BOARD_LCD_SPI_IO_DATA3           41
#define BOARD_LCD_SPI_IO_CS              21
#define BOARD_LCD_GPIO_RST                9
#define BOARD_LCD_POW_EN_GPIO            12   // 屏电源使能（143C 无此脚）
#define BOARD_LCD_GPIO_BACKLIGHT         13
#define BOARD_LCD_BACKLIGHT_LEDC_INVERT   0
#define BOARD_LCD_RGB565_TX_BYTE_SWAP     0
#define BOARD_LCD_SPI_PCLK_HZ   (80 * 1000 * 1000)
#define BOARD_LCD_RGB_ORDER_BGR           0
#define BOARD_ST77916_VENDOR_MADCTL ((uint8_t)(0x60u))

// ---- 电源 / 按键 ----
#define BOARD_BAT_CONTROL_GPIO            7   // 电池供电锁存，高电平保持
#define BOARD_BAT_CONTROL_LATCH_LEVEL     1
#define BOARD_KEY_GPIO                    6   // 电源键，低有效（143C 是 BOOT=GPIO0）

// ---- I2C (触摸 CST816 / IMU QMI8658 共用) ----
#define BOARD_I2C_SCL                   10
#define BOARD_I2C_SDA                   11
#define BOARD_I2C_PORT                   1
#define BOARD_I2C_FREQ_HZ           400000
#define BOARD_TOUCH_GPIO_INT             4
#define BOARD_TOUCH_GPIO_RST             5

// ---- 麦克风 I2S ----
#define BOARD_MIC_I2S_GPIO_BCLK         16
#define BOARD_MIC_I2S_GPIO_WS            2
#define BOARD_MIC_I2S_GPIO_DIN          17

// ---- 喇叭 I2S（无 ES8311 codec，直推 Class-D 功放）----
#define BOARD_SPK_I2S_GPIO_BCLK         48
#define BOARD_SPK_I2S_GPIO_WS           38
#define BOARD_SPK_I2S_GPIO_DOUT         47
#define BOARD_SPK_I2S_GPIO_MCLK         15
#define BOARD_AUDIO_PA_EN_GPIO           3
#define BOARD_AUDIO_PA_EN_ACTIVE_LEVEL   1

// ============================================================================
// 对照：工作区 boards/185b/MotoNav/pin_config.h 与本板几乎完全一致
//   QSPI D0-D3 46/45/42/41、SCK 40、CS 21、TE 18、I2C SDA 11/SCL 10、TP_INT 4  -> 全部吻合
//   差异仅 3 处：LCD_RESET 3->9、LCD_BL 5->13、TP_RESET 1->5
//   并且 185b 已有 ST77916 初始化序列(Board185BInit.h)、
//   466 逻辑画布 -> 360 物理屏缩放(PortScale.h)、CST816 触摸(TouchDrvCST816)。
//   => 移植路线：以 boards/143 (v7.25 功能集) 为主体，
//      替换显示层为 185b 的 ST77916/PortScale，触摸换 CST816，
//      按上表修正电源键/电池/音频，Flash 改 32MB 分区。
//
// 与 143C 的主要不兼容点（直接烧 143C 固件必然黑屏）：
//   屏 466x466 CO5300 -> 360x360 ST77916；QSPI 引脚整组不同；
//   CST820@0x15 -> CST816；I2C 47/48 -> 11/10；BOOT GPIO0 -> 电源键 GPIO6；
//   电池 ADC GPIO4/STAT GPIO7 -> GPIO7 锁存；ES8311 -> 直推 I2S 功放；8MB -> 32MB Flash。
// ============================================================================
