#pragma once
// 首页骑手/动画人物与等待画面。由 MotoNav.ino 按依赖顺序包含，仅编译一次。
// ================= 整屏画面 =================

#define WAIT_READY  0
#define WAIT_NOLINK 1
#define WAIT_LOST   2

// ================= 待机画面两套共存（v7.18） =================
// homeBgMode = 0：CC110 便利店像素风背景（时钟在富士山顶上方天空、日期白字居中、
//                   ready 在日期下、蓝牙名+版本一行在车轮下地面、电池/蓝牙点居中置于版本号上方）
// homeBgMode = 1：原版黑底 + 动画人物（thumbs_up.gif 点赞小人）+ cc110 摩托（motorcycle_running.gif）。
//                  点赞小人替换原版小人，cc110 摩托替换原机车，两者均按原尺寸叠加居中；
//                  时钟/日期/ready/版本/电池位置与模式 0 完全一致（文字信息不变）。
//                  点赞小人：点赞一次（7 帧 130ms）→ 停顿 3 秒 → 循环；cc110 摩托：一直循环播放（10 帧 90ms）。
// 待机状态下 BOOT 短按在两种待机画面之间切换。
#include "../../resources/ThumbsUp.h"       // 模式 1 动画人物：thumbs_up.gif（7 帧 RGB565 + 透明掩码）
#include "../../resources/Motorcycle.h"     // 模式 1 动画机车：motorcycle_running.gif（10 帧 RGB565 + 透明掩码，白底透明）
#include "../../resources/Cc110Background.h"
#include "../../HomeClock.h"
HomeClock homeClock;
char homeClockTime[6]={};
char homeClockDate[11]={};
bool onWaitScreen = false;
bool sliding = false;                       // v5.5 正在拖动/滑动换页：别的画面先别画
int waitSt = 0;
bool catForce = false;                      // 首页刚重画：马上画一次
int homeBgMode = 1;                         // 1 = 黑底动画人物（thumbs_up 点赞小人）+ cc110 摩托（默认待机画面）；
                                            // 0 = CC110 便利店像素风背景（富士山顶 + 7-11，即“富士山711摩托”）
uint32_t standbyIdleSince = 0;              // 进入默认待机(mode1)或最近一次操作的时间；满 1 分钟无操作自动切到 mode0
// 模式 1 布局：点赞小人(左) + cc110 摩托(右)，底部对齐、整组水平居中于屏幕
// 注：THUMB_W / THUMB_H 由 ThumbsUp.h 定义（100 / 160），此处不再重复定义
#define MOTO_W   180
#define MOTO_H   116
#define ANIM_GAP 6                           // 两者水平间距（更近）
#define ANIM_CX  CX                          // 整组水平中心
#define THUMB_X0 (ANIM_CX - (THUMB_W + ANIM_GAP + MOTO_W) / 2)   // 左侧：点赞小人
#define MOTO_X0  (THUMB_X0 + THUMB_W + ANIM_GAP)                 // 右侧：cc110 摩托
#define ANIM_BOT 343                         // 两者共同底部（底部对齐，画面中部偏下，避开上文字与下电池）
#define THUMB_Y0 (ANIM_BOT - THUMB_H)        // 183
#define MOTO_Y0  (ANIM_BOT - MOTO_H - 12)    // 215
// 动画整块擦除区域（取两者外接矩形，每帧先黑底擦除再分别画）
#define ANIM_X   THUMB_X0
#define ANIM_Y   THUMB_Y0
#define ANIM_W   (THUMB_W + ANIM_GAP + MOTO_W)
#define ANIM_H   THUMB_H

// 把第 f 帧直接写进帧缓冲（透明像素跳过，保留黑底）。仅在模式 1 使用。
// 注意：整块擦除由 drawStandbyAnim() 统一在画之前完成，这里只负责写像素。
void drawThumbFrame(int f) {
  const int x0 = THUMB_X0, y0 = THUMB_Y0;
  uint16_t *fb = (uint16_t*)gfx->getFramebuffer();
  if (!fb) return;
  const uint16_t *rgb = THUMB_RGB + (size_t)f * THUMB_W * THUMB_H;
  const uint8_t *msk = THUMB_MASK + (size_t)f * THUMB_FPW;
  for (int y = 0; y < THUMB_H; y++) {
    for (int x = 0; x < THUMB_W; x++) {
      int idx = y * THUMB_W + x;
      if (msk[idx >> 3] & (1 << (idx & 7))) {
        fb[(y0 + y) * CC110_BG_W + (x0 + x)] = rgb[idx];
      }
    }
  }
  needFlush = true;
}

// 把第 f 帧 cc110 摩托直接写进帧缓冲（白底透明像素跳过）。仅在模式 1 使用。
void drawMotoFrame(int f) {
  const int x0 = MOTO_X0, y0 = MOTO_Y0;
  uint16_t *fb = (uint16_t*)gfx->getFramebuffer();
  if (!fb) return;
  const uint16_t *rgb = MOTO_RGB + (size_t)f * MOTO_W * MOTO_H;
  const uint8_t *msk = MOTO_MASK + (size_t)f * MOTO_FPW;
  for (int y = 0; y < MOTO_H; y++) {
    for (int x = 0; x < MOTO_W; x++) {
      int idx = y * MOTO_W + x;
      if (msk[idx >> 3] & (1 << (idx & 7))) {
        fb[(y0 + y) * CC110_BG_W + (x0 + x)] = rgb[idx];
      }
    }
  }
  needFlush = true;
}

// 每 0.1 秒检查一次（函数名沿用 v3.6）。仅模式 1 运行：
//   点赞小人：点赞一次（7 帧 ×130ms）→ 停顿 3 秒（末帧定格）→ 再循环；
//   cc110 摩托：一直循环播放（10 帧 ×90ms）。
void drawStandbyAnim() {
  if (!onWaitScreen || sliding || homeBgMode != 1) return;
  static int lmf = -1, lpf = -1;
  int mf = (int)((millis() / 90) % MOTO_FRAMES);             // 摩托：一直循环
  const unsigned long cyc = 910UL + 3000UL;                  // 点赞：一次 7×130 + 停顿 3000
  const unsigned long t = millis() % cyc;
  int pf = (t < 910) ? (int)(t / 130) : (THUMB_FRAMES - 1);  // 点赞一次后停 3 秒（末帧定格）
  if (mf == lmf && pf == lpf && !catForce) return;
  catForce = false; lmf = mf; lpf = pf;
  gfx->fillRect(ANIM_X, ANIM_Y, ANIM_W, ANIM_H, C_BLACK);     // 先整块擦掉上一帧
  drawMotoFrame(mf);                                          // 先画摩托
  drawThumbFrame(pf);                                         // 再画点赞小人（叠在上层）
  needFlush = true;
}

// 整屏贴背景图（模式 0）：直接把 flash 里的 RGB565 整幅拷进当前帧缓冲
void drawHomeBackground() {
  uint16_t *fb = (uint16_t *)gfx->getFramebuffer();
  if (fb) memcpy(fb, CC110_BG, CC110_BG_W * CC110_BG_H * sizeof(uint16_t));
}

#include "../../FixedPageLayout.h"
// 固定位置：音乐在左、导航居中、天气在右；降雨在天气上方、预警在下方。
int page = 0;                               // 0 = 导航 / 首页，1 = 天气，2 = 音乐
int wxDetail = 0;                           // 天气列：0 = 天气，1 = 上方降雨，2 = 下方预警
unsigned long pageAt = 0;

// 页面顺序对应滑动位置：音乐在左，主页居中，天气在右；导航主页不显示。
void drawPageDots() {
  if (page == 0 && inNav) return;
  const int pageOrder[] = {2, 0, 1};
  for (int i = 0; i < 3; i++) {
    aaDisc(CX + (i - 1) * 20, 444, 4, page == pageOrder[i] ? C_WHITE : 0x4208);
  }
  if (page == 1) for (int i = 0; i < 3; i++) {
    aaDisc(438, CX + (i - 1) * 18, 3,
      i == FixedPageLayout::weatherRow(wxDetail) ? C_WHITE : 0x4208);
  }
}

// 文字层：两种待机画面共用同一套位置（时钟/日期/ready/版本/电池不变）
void drawHomeClock(bool force=false) {
  if(!onWaitScreen||sliding||page!=0||inNav)return;
  static int64_t drawnMinute=-2;
  const uint32_t now=millis();
  const int64_t minute=homeClock.minuteKey(now);
  if(!force&&minute==drawnMinute)return;
  char time[6],date[11];
  const bool valid=homeClock.format(now,time,date);
  drawnMinute=minute;
  if(!force&&strcmp(time,homeClockTime)==0&&strcmp(date,homeClockDate)==0)return;
  memcpy(homeClockTime,time,sizeof(homeClockTime));memcpy(homeClockDate,date,sizeof(homeClockDate));
  // 擦除旧字：模式 0 透明底（从背景图拷贝），模式 1 黑底
  const int x0=CX-159, y0=38, w=318, h=92;
  uint16_t *fb=(uint16_t*)gfx->getFramebuffer();
  if(homeBgMode==1) gfx->fillRect(x0,y0,w,h,C_BLACK);
  else if(fb) memcpy(fb + y0*CC110_BG_W + x0, CC110_BG + y0*CC110_BG_W + x0, w*h*sizeof(uint16_t));
  printCentered(time, CX, 44, 8, valid?C_WHITE:C_GREY);   // 时间白字居中
  printCenteredBold(date, CX, 112, 2, C_WHITE);           // 日期：加粗、居中、白色
  needFlush=true;
}

// 共用：底部状态行（ready / no link / LINK LOST）
void drawWaitStatus(int st) {
  if (st == WAIT_READY) {
    printCentered("ready", CX, 134, 2, C_GREEN);
  } else if (st == WAIT_NOLINK) {
    printCentered("no link", CX, 134, 2, C_GREY);
  } else {
    drawRedRing();
    printCentered("LINK LOST", CX, 134, 2, C_RED);
  }
}

void drawWaiting(int st) {
  waitSt = st;
  if (sliding) return;                                                 // v5.5 正在滑动：滑完再画
  if (page == 2) { onWaitScreen = false; musicDirty = true; return; }
  if (page == 1) { onWaitScreen = false; wxDirty = true; return; }   // v5.3 正在看天气：先不画首页，滑回来时再画
  onWaitScreen = true; waitSt = st;

  if (homeBgMode == 1) {
    // 原版黑底 + 动画（点赞小人 + cc110 摩托）；文字位置与模式 0 一致
    gfx->fillScreen(C_BLACK);
    catForce = true;
    drawStandbyAnim();                     // 画首帧动画（摩托循环 + 小人点赞一次停 3 秒）
    drawHomeClock(true);
    drawWaitStatus(st);
    // 蓝牙名 + 固件版本合成一行：与模式 0 同一位置（底部）
    printCentered(bleName + "  fw " + FW_VERSION + "  " + String(qspiMHz) + "MHz", CX, 430, 1, C_GREY);
    drawStatusBottom();                    // 电池/蓝牙点整组居中放到版本号上方（黑底自擦）
    drawPageDots();
    needFlush = true;
  } else {
    // 模式 0：CC110 像素风背景（原布局，完全不变）
    drawHomeBackground();
    drawHomeClock(true);
    drawWaitStatus(st);
    printCentered(bleName + "  fw " + FW_VERSION + "  " + String(qspiMHz) + "MHz", CX, 430, 1, C_GREY);
    drawStatusBottom();
    drawPageDots();
    needFlush = true;
  }
}
