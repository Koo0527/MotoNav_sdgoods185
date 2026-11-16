#pragma once
// v7.28.7 OMEGA Speedmaster Snoopy 50周年纪念表盘（待机模式 2，homeBgMode==2）
// 底图：resources/OmegaDial.h（466x466，8bpp 256色 + PackBits RLE），首次使用解码进 PSRAM；
// 中央时针/分针/秒针按手机同步时间实时绘制（aaDisc 胶囊笔刷，1Hz 走针）；
// 三个小盘指针为 12 点归零态（计时停走，与原版照片一致，烘焙在底图中）。
// 纯表盘 1:1 临摹：不叠加时钟文字/状态行/电池/页点。
#include <stdint.h>
#include <string.h>
#include "../../resources/OmegaDial.h"

static uint16_t *omegaBg = nullptr;        // PSRAM 解码后的 466x466 RGB565
static bool omegaBgOk = false;
static uint16_t omegaPal565[256];
static int64_t omegaLastSec = -1;

// ---- 颜色（与生成脚本一致） ----
#define OMEGA_CX 233
#define OMEGA_CY 233
static const uint16_t OMEGA_HB = 0x19F0;   // 指针蓝  RGB(27,62,133)
static const uint16_t OMEGA_WH = 0xF79D;   // 夜光白  RGB(242,243,238)
static const uint16_t OMEGA_SV = 0xCE7A;   // 银色轮毂 RGB(200,204,208)
static const uint16_t OMEGA_DK = 0x1906;   // 轮毂深芯 RGB(30,35,50)

// PackBits RLE + 256 色调色板 -> RGB565（PSRAM）
static bool omegaEnsure() {
  if (omegaBgOk) return true;
  if (!omegaBg) omegaBg = (uint16_t *)ps_malloc((size_t)OMEGA_DIAL_W * OMEGA_DIAL_H * sizeof(uint16_t));
  if (!omegaBg) { Serial.println("[omega] PSRAM alloc fail"); return false; }
  for (int i = 0; i < 256; i++) {
    uint8_t r = pgm_read_byte(&OMEGA_DIAL_PAL[i * 3 + 0]);
    uint8_t g = pgm_read_byte(&OMEGA_DIAL_PAL[i * 3 + 1]);
    uint8_t b = pgm_read_byte(&OMEGA_DIAL_PAL[i * 3 + 2]);
    omegaPal565[i] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
  }
  const int total = OMEGA_DIAL_W * OMEGA_DIAL_H;
  const uint8_t *p = OMEGA_DIAL_RLE;
  const uint8_t *end = p + OMEGA_DIAL_RLE_LEN;
  int o = 0;
  while (o < total && p < end) {
    uint8_t b = pgm_read_byte(p++);
    if (b < 0x80) {                                    // 字面量段：b+1 个索引
      for (int k = 0; k <= b && o < total; k++)
        omegaBg[o++] = omegaPal565[pgm_read_byte(p++)];
    } else if (b > 0x80) {                             // 重复段：b-0x7F 个同色
      uint16_t c = omegaPal565[pgm_read_byte(p++)];
      int rep = b - 0x80 + 1;
      while (rep-- > 0 && o < total) omegaBg[o++] = c;
    }
  }
  omegaBgOk = (o == total);
  if (!omegaBgOk) Serial.printf("[omega] RLE decode short: %d/%d\n", o, total);
  else Serial.println("[omega] dial decoded to PSRAM (434KB)");
  return omegaBgOk;
}

// aaDisc 胶囊笔刷：沿 (x1,y1)-(x2,y2) 每 1px 盖一个抗锯齿圆盘
static void omegaCapsule(float x1, float y1, float x2, float y2, float w, uint16_t c) {
  float dx = x2 - x1, dy = y2 - y1;
  float len = sqrtf(dx * dx + dy * dy);
  int steps = (int)ceilf(len);
  if (steps < 1) steps = 1;
  for (int i = 0; i <= steps; i++) {
    float t = (float)i / steps;
    aaDisc(x1 + dx * t, y1 + dy * t, w * 0.5f, c);
  }
}

// 一根针：0=12点顺时针角度；tail 为反向尾部长度；cStripe 宽度 0 则不画白芯
static void omegaHand(float angDeg, float len, float w, float tail,
                      uint16_t cBody, uint16_t cStripe, float stripeW) {
  float a = radians(angDeg);
  float sa = sinf(a), ca = cosf(a);
  float tipx = OMEGA_CX + len * sa,  tipy = OMEGA_CY - len * ca;
  float tlx  = OMEGA_CX - tail * sa, tly  = OMEGA_CY + tail * ca;
  omegaCapsule(tlx, tly, tipx, tipy, w, cBody);
  if (stripeW > 0.5f)
    omegaCapsule(OMEGA_CX, OMEGA_CY,
                 OMEGA_CX + (len - 6) * sa, OMEGA_CY - (len - 6) * ca, stripeW, cStripe);
}

// 按时分秒画三根中央针（时针/分针蓝棒白芯，中央秒针细蓝针带尾部配重）
static void omegaDrawHands(int hh, int mm, int ss) {
  float ah = (hh % 12) * 30.0f + mm * 0.5f;
  float am = mm * 6.0f + ss * 0.1f;
  float as = ss * 6.0f;
  omegaHand(ah, 148, 10, 26, OMEGA_HB, OMEGA_WH, 5);           // 时针：尖 ~0.63R（到时标内端）
  omegaHand(am, 182, 8, 36, OMEGA_HB, OMEGA_WH, 4);            // 分针：尖 ~0.78R（到分钟轨道）
  omegaHand(as, 196, 3, 55, OMEGA_HB, OMEGA_HB, 0);            // 大秒针带尾部
  float a = radians(as);
  float tx = OMEGA_CX - 58 * sinf(a), ty = OMEGA_CY + 58 * cosf(a);
  aaDisc(tx, ty, 8, OMEGA_HB);                                  // 尾部配重
  aaDisc(OMEGA_CX, OMEGA_CY, 11, OMEGA_SV);                     // 轮毂
  aaDisc(OMEGA_CX, OMEGA_CY, 4.5f, OMEGA_DK);
}

// 整幅渲染：底图 + 三根指针
static void omegaRender(int hh, int mm, int ss) {
  uint16_t *fb = (uint16_t *)gfx->getFramebuffer();
  if (!fb || !omegaBgOk) return;
  memcpy(fb, omegaBg, (size_t)OMEGA_DIAL_W * OMEGA_DIAL_H * sizeof(uint16_t));
  omegaDrawHands(hh, mm, ss);
  needFlush = true;
}

static bool omegaUnsyncLogged = false;

// 1Hz 走针（在 loop 的待机动画节拍处调用）；force=true 立即重画
static void omegaHandsTick(bool force = false) {
  if (!onWaitScreen || sliding || homeBgMode != 2 || page != 0 || inNav) return;
  if (!omegaEnsure()) return;                                   // PSRAM/解码失败由 drawWaiting 回退屏保
  int64_t sec = homeClock.localSeconds(millis());
  if (sec < 0) {
    // v7.28.8 修复：时间未同步（手机未发 #TIME）时也显示表盘，指针停在 12 点
    if (force || omegaLastSec != -2) {
      omegaLastSec = -2;
      omegaRender(12, 0, 0);
      if (!omegaUnsyncLogged) {
        omegaUnsyncLogged = true;
        Serial.println("[omega] dial shown, time not synced -> hands at 12:00");
      }
    }
    return;
  }
  if (!force && sec == omegaLastSec) return;
  omegaLastSec = sec;
  int64_t daysec = sec % 86400;
  omegaRender((int)((daysec / 3600) % 24), (int)((daysec / 60) % 60), (int)(daysec % 60));
}
