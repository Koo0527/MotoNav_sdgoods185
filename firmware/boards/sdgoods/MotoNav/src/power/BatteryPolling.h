#pragma once
// 电量轮询及状态消息。由 MotoNav.ino 按依赖顺序包含，仅编译一次。
String pendingStat2 = "";
// v6.8 每 5 秒读一次；有变化就重画、通知手机
void batPoll() {
#if defined(BOARD_SDGOODS)
  // 谷仓：GPIO8 (ADC1_CH7) 读 1/3 分压点，口径与官方 sdgoods_hw_bat_v() 一致；
  // 电量按官方 UI 的 3.00V=0% / 4.28V=100% 线性映射。本板无充电/VBUS 状态脚。
  unsigned long now0 = millis();
  if (batAt && now0 - batAt < 5000) return;
  batAt = now0;
  uint32_t mv = (uint32_t)analogReadMilliVolts(BOARD_BAT_ADC_PIN) *
                (uint32_t)BOARD_BAT_DIVIDER;
  batMv = (int)mv;
  int pct0 = (int)constrain((long)(mv - 3000) * 100 / (4280L - 3000L), 0, 100);
  bool chg0 = false, usb0 = false;
  bool changed0 = pct0 != batPct || chg0 != batChg || usb0 != usbIn;
  batPct = pct0; batChg = chg0; usbIn = usb0;
  if (mediaHid && changed0) mediaHid->setBatteryLevel((uint8_t)constrain(pct0, 0, 100));
  if (changed0) {
    if (onWaitScreen && page == 0 && !sliding) drawStatusBottom();
    else if (inNav) navDirty = true;
  }
  if (changed0 || now0 - batSentAt > 60000) {
    batSentAt = now0;
    pendingBat = "V," + String(batPct) + ",0,0," + String(batMv);
  }
  return;
#elif defined(BOARD_143C)
  // 1.43C：ADC 读电池电压（1/2 分压），ETA6098 STAT 判充电；无独立 VBUS 检测，插线即按充电算
  unsigned long now0 = millis();
  if (batAt && now0 - batAt < 5000) return;
  batAt = now0;
  uint32_t mv = (uint32_t)analogReadMilliVolts(BATT_ADC_PIN) * (uint32_t)BATT_DIVIDER;
  batMv = (int)mv;
  // 电量标定：空电 3.3V(3300mV)→0%，满电 4.14V(4140mV，ETA6098 实际截止点)→100%。
  // 原 4200mV 偏高，导致充满只显示 93~94%；按真实满电电压标定。
  int pct0 = (int)constrain((long)(mv - 3300) * 100 / (4140L - 3300L), 0, 100);
  bool chg0 = digitalRead(BATT_STAT_PIN) == LOW;
  bool usb0 = chg0;
  bool changed0 = pct0 != batPct || chg0 != batChg || usb0 != usbIn;
  batPct = pct0; batChg = chg0; usbIn = usb0;
  if (mediaHid && changed0) mediaHid->setBatteryLevel((uint8_t)constrain(pct0, 0, 100));
  if (changed0) {
    if (onWaitScreen && page == 0 && !sliding) drawStatusBottom();
    else if (inNav) navDirty = true;
  }
  if (changed0 || now0 - batSentAt > 60000) {
    batSentAt = now0;
    pendingBat = "V," + String(batPct) + "," + String(batChg ? 1 : 0) + "," + String(usbIn ? 1 : 0) + "," + String(batMv);
  }
  return;
#else
  if (!pmuOK) return;
  unsigned long now = millis();
  if (batAt && now - batAt < 5000) return;
  batAt = now;
  int s1 = pmuRead(0x00), s2 = pmuRead(0x01);
  if (s1 < 0 || s2 < 0) return;
  bool present = (s1 >> 3) & 1;
  int pct = -1;
  if (present) {
    int v = pmuRead(0xA4);
    if (v >= 0 && v <= 100) pct = v;
    int h = pmuRead(0x34), l = pmuRead(0x35);
    if (h >= 0 && l >= 0) batMv = ((h & 0x1F) << 8) | l;
  }
  // v7.2 偶尔读不到电池：先沿用上次的电量，连续 3 次读不到才当成没接电池
  static int missCnt = 0;
  if (pct < 0) { if (batPct >= 0 && ++missCnt < 3) { pct = batPct; present = true; } }
  else missCnt = 0;
  bool chg = present && ((s2 >> 5) & 3) == 1;
  bool usb = ((s1 >> 5) & 1) && !((s2 >> 3) & 1);
  bool changed = pct != batPct || chg != batChg || usb != usbIn;
  batPct = pct; batChg = chg; usbIn = usb;
  if (mediaHid && changed && pct >= 0) mediaHid->setBatteryLevel((uint8_t)constrain(pct, 0, 100));
  if (changed) {
    if (onWaitScreen && page == 0 && !sliding) drawStatusBottom();        // v7.2 只重画状态栏（首页底部）
    else if (inNav) navDirty = true;
  }
  if (changed || now - batSentAt > 60000) {
    batSentAt = now;
    pendingBat = "V," + String(batPct) + "," + String(batChg ? 1 : 0) + "," + String(usbIn ? 1 : 0) + "," + String(batMv);
  }
#endif
}
