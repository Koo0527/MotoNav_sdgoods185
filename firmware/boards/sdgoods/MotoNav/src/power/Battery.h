#pragma once
// 电源芯片读取及外接电源检测。由 MotoNav.ino 按依赖顺序包含，仅编译一次。
// ================= v6.8 电池（AXP2101 电源芯片，和触摸屏共用 I2C，地址 0x34）=================
#define PMU_ADDR 0x34
bool pmuOK = false;
int batPct = -1;                       // -1 = 没接电池
bool batChg = false, usbIn = false;
int batMv = 0;
unsigned long batAt = 0, batSentAt = 0;
String pendingBat = "";

int pmuRead(uint8_t reg) {
  Wire.beginTransmission(PMU_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom((uint8_t)PMU_ADDR, (uint8_t)1) != 1) return -1;
  return Wire.read();
}
void pmuSetBit(uint8_t reg, uint8_t bit) {
  int v = pmuRead(reg);
  if (v < 0 || (v & (1 << bit))) return;
  Wire.beginTransmission(PMU_ADDR);
  Wire.write(reg);
  Wire.write((uint8_t)(v | (1 << bit)));
  Wire.endTransmission();
}

// 开机时调一次：只打开电量计、电池检测、电池电压测量，不动任何供电设置
void batInit() {
#if defined(BOARD_SDGOODS)
  // 谷仓次元屏：无 AXP2101 / 无充电状态脚。电池电压走 GPIO8 (ADC1_CH7，1/3 分压)，
  // 供电自锁闩 GPIO7、电源键 GPIO6、屏电源 GPIO12、背光 GPIO13 已由
  // boardPowerInit() / BoardSdgoodsPanel::tftInit() / boardBrightnessInit() 配置。
  pmuOK = false;
  analogReadResolution(12);
#elif defined(BOARD_143C)
  // 1.43C：无 AXP2101，电池电压走 GPIO4 ADC（板载 1/2 分压），充电状态走 GPIO7（ETA6098 STAT）
  pmuOK = false;
  pinMode(BATT_STAT_PIN, INPUT_PULLUP);
  analogReadResolution(12);
#else
  pmuOK = pmuRead(0x00) >= 0;
  if (!pmuOK) return;
  pmuSetBit(0x18, 3);                  // 电量计
  pmuSetBit(0x68, 0);                  // 电池检测
  pmuSetBit(0x30, 0);                  // 电池电压测量
#endif
}

// AXP2101 STATUS1 bit 5 is VBUS-good, independent of charging/full state.
// Unknown/stale power readings keep the device awake, rather than guessing battery.
void idlePowerPollSupply(bool force = false) {
  const uint32_t now = millis();
  if (!force && idlePowerSupply.sampled &&
      (uint32_t)(now - idlePowerSupply.checkedAt) < 500) return;
  const bool wasBattery = idlePowerSupplyAllowsOff(&idlePowerSupply, now);
#ifdef BOARD_143C
  // 1.43C：没有 AXP2101，用 ETA6098 STAT 合成 STATUS1：充电中（STAT 低）= VBUS 在位（bit5）
  const int status = (digitalRead(BATT_STAT_PIN) == LOW) ? 0x20 : 0x00;
#elif defined(BOARD_SDGOODS)
  // 谷仓没有独立的 VBUS/充电检测脚，状态未知。
  // idlePowerSupplyUpdate(-1) 会置 batteryOnly=false => 五分钟自动关机保持禁用（安全），
  // 只有在 launcher 里由官方电源管理负责关机。
  const int status = -1;
#else
  const int status = pmuOK ? pmuRead(0x00) : -1;
#endif
  idlePowerSupplyUpdate(&idlePowerSupply, now, status);
  // Unplugging / recovering a valid battery-only reading starts a full interval.
  if (!wasBattery && idlePowerSupplyAllowsOff(&idlePowerSupply, now))
    idlePowerOffReset(&idlePowerOffTimer, now);
}

// 小电池图标：x0,y0 = 左上角（电池身 24x12，右边小凸头，再右边是百分比数字）
