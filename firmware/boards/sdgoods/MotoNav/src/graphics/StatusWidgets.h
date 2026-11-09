#pragma once
// 电池、连接状态、车速和限速牌。由 MotoNav.ino 按依赖顺序包含，仅编译一次。
#include "../../resources/Cc110Background.h"   // drawStatusBottom 透明底擦除需 CC110_BG
void drawBattery(int x0, int y0, int s = 1, bool backing = false) {
  if (batPct < 0) return;                                  // 没接电池不画
  if (backing) {                                           // v7.2 小地图上垫一块黑底
    int tw = String(batPct).length() * 6 * s;
    gfx->fillRoundRect(x0 - 4*s, y0 - 3*s, 24*s + 2 + 5*s + tw + 7*s, 12*s + 6*s, 6*s, C_BLACK);
  }
  uint16_t c = batChg ? C_CYAN : batPct > 50 ? C_GREEN : batPct >= 20 ? 0xFE60 : C_RED;
  if (!batChg && batPct < 10 && (millis() / 600) % 2) c = 0x4208;   // 快没电：闪
  const int W = 24 * s, H = 12 * s;
  gfx->drawRoundRect(x0, y0, W, H, 3 * s, c);
  gfx->drawRoundRect(x0 + 1 * s, y0 + 1 * s, W - 2 * s, H - 2 * s, 2 * s, c);
  gfx->fillRect(x0 + W, y0 + 3 * s, 2 * s, H - 6 * s, c);
  int fw = (W - 6 * s) * constrain(batPct, 0, 100) / 100;
  if (fw > 0) gfx->fillRect(x0 + 3 * s, y0 + 3 * s, fw, H - 6 * s, c);
  if (batChg) {                                            // 小闪电
    int bx = x0 + W / 2, by = y0 + H / 2;
    gfx->fillTriangle(bx + 1 * s, by - 5 * s, bx - 3 * s, by + 1 * s, bx, by + 1 * s, C_BLACK);
    gfx->fillTriangle(bx - 1 * s, by + 5 * s, bx + 3 * s, by - 1 * s, bx, by - 1 * s, C_BLACK);
  }
  gfx->setTextSize(s);
  gfx->setTextColor(c);
  gfx->setCursor(x0 + W + 5 * s, y0 + 2 * s);
  gfx->print(String(batPct));
}

void drawStatus() {
  gfx->fillRect(CX - 9, 21, 9 + 14 + 24 + 2 + 5 + 18 + 4, 19, C_BLACK);   // v7.2 先擦掉旧的（只重画状态栏时用）
  aaDisc(CX, 30, 7, bleConnected ? C_GREEN : C_GREY);
  drawBattery(CX + 14, 24);                               // v6.8 电量（蓝牙小圆点右边）
  needFlush = true;
}

// 首页专用：电池 + 蓝牙点放到屏幕底部版本号上方，整组水平居中（电池图标准 2 倍放大）
void drawStatusBottom() {
  const int s = 2;            // 放大一倍
  const int y0 = 400;         // 上移避免压到版本行（电池高 24，底 424 < 版本行 430）
  // 擦除旧电池/蓝牙点：模式 1（黑底骑手动画）用黑块，模式 0（CC110 背景）从背景图透明拷贝
  const int bx0 = CX - 70, by0 = y0 - 4, bw = 120, bh = 32;
  uint16_t *fb = (uint16_t*)gfx->getFramebuffer();
  extern int homeBgMode;
  if (homeBgMode == 1) {
    gfx->fillRect(bx0, by0, bw, bh, C_BLACK);
  } else if (fb) {
    memcpy(fb + by0*CC110_BG_W + bx0, CC110_BG + by0*CC110_BG_W + bx0, bw*bh*sizeof(uint16_t));
  }
  aaDisc(CX - 58, y0 + 12, 7, bleConnected ? C_GREEN : C_GREY);   // 蓝牙点（整组左侧）
  drawBattery(CX - 32, y0, s);                                    // 电池图标（2 倍放大，累计右移 15px）
  needFlush = true;
}

// ================= 车速和限速牌 =================

void drawSpeedBlock(bool over) {
  if (speedKmh < 0) return;
  int sx = hasCam ? 200 : CX;
  printCentered(String(speedKmh), sx, SPEED_Y, 5, over ? C_RED : C_WHITE);   // 每个字符 30x40
  printCentered("km/h", sx, SPEED_Y + 46, 2, over ? 0xFC10 : C_GREY);
  if (hasCam) {
    const int bx = 298, by = 76;
    aaDisc(bx, by, 28, C_SIGNRED);
    aaDisc(bx, by, 21, C_WHITE);
    String lim = String(camLimit);
    int sz = lim.length() >= 3 ? 2 : 3;
    printCentered(lim, bx, by - 4 * sz, sz, C_BLACK);
    printCentered(distText(camDist), bx, by + 34, 2, over ? 0xFC10 : C_GREY);
  }
}
