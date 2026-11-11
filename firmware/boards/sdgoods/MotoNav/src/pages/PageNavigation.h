#pragma once
// 音乐页面、手势换页和数据中断提示。由 MotoNav.ino 按依赖顺序包含，仅编译一次。
// ---- v5.5 跟手换页 ----
// 触摸坐标和画面方向相反时改成 1（拖动时画面往手指反方向走就是反了）
// SDGOODS：MADCTL=0x60 造成的「X 翻转 + XY 互换」已在 IncomingCall.h 的
// panelPointToLogical() 里按官方 BSP 处理，这里只负责软件自由旋转，故保持 0。
#define TOUCH_FLIP_X 0
#define TOUCH_FLIP_Y 0
// 起拖阈值与点按判据同源（IncomingCall.h 的 TOUCH_TAP_SLOP = 31 逻辑 px ≈ 官方 24 物理 px）。
// 两者必须相等，否则位移落在中间区间时既算滑动又算点按，点击会被吞掉 / 翻页乱抢。
#define DRAG_START  TOUCH_TAP_SLOP
uint16_t *slideOld = nullptr, *slideNew = nullptr;
int16_t chordHalf[LCD_HEIGHT];   // 每一行在圆里的半宽（圆外不用算）
bool chordReady = false;
void drawNavFrame();

// ===== v7.28.5 方块封面音乐页 =====
// 版式（466 逻辑画布，圆屏半径 233）：
//   背景 = 封面主色压暗铺满（圆屏不露黑边）
//   封面方块 180x180 @ (143,42)：四角最远 (±90,+191) ≈ 211 < 233，不出圆
//   歌名 y=234 / 时长 y=276 / 歌词（≤3 行）y=300..356
//   三个控制按键固定在 y=400（原播放中会隐藏，v7.28.5 起常驻）
#define COVER_SZ 180
#define COVER_Y0 42
#define COVER_X0 (CX - COVER_SZ / 2)
#define TITLE_Y   234
#define TIME_Y    276
#define LYRIC_Y   302

static inline uint16_t dimC(uint16_t c, uint32_t k) {     // k: 0~32，32 = 原样
  uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  return (uint16_t)(((r * k >> 5) << 11) | ((g * k >> 5) << 5) | (b * k >> 5));
}

// 从歌名+专辑名生成稳定的主色（HSV，中等饱和/亮度，避免过曝或全黑）
static uint16_t musicCoverColor() {
  String src = appleMedia.available ? appleMedia.values[0].text + appleMedia.values[2].text : "";
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < src.length(); ++i) { h ^= (uint8_t)src[i]; h *= 16777619u; }
  const float hue = (h % 360) / 360.0f, s = 0.55f, v = 0.60f;
  const int i = (int)(hue * 6); const float f = hue * 6 - i;
  const float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
  float r, g, b;
  switch (i % 6) {
    case 0: r = v; g = t; b = p; break;  case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;  case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;  default: r = v; g = p; b = q; break;
  }
  return ((uint16_t)(r * 255) >> 3 << 11) | ((uint16_t)(g * 255) >> 2 << 5) | ((uint16_t)(b * 255) >> 3);
}

// 把 4 位灰度图槽位按“cover”方式铺满 S×S 方块（等比放大、居中裁切）
void drawCoverArt(int slot, int x0, int y0, int S) {
  TextImg &im = imgs[slot];
  if (!im.show || im.showW == 0 || im.showH == 0) return;
  const float s = fmaxf((float)S / im.showW, (float)S / im.showH);
  const int dw = (int)(im.showW * s), dh = (int)(im.showH * s);
  const int cx0 = x0 - (dw - S) / 2, cy0 = y0 - (dh - S) / 2;
  const float inv = 1.0f / s, bpr = (im.showW + 1) / 2;
  for (int dy = 0; dy < dh; dy++) {
    const int py = cy0 + dy;
    if (py < y0 || py >= y0 + S) continue;
    int sy0 = (int)(dy * inv), sy1 = (int)((dy + 1) * inv);
    if (sy1 <= sy0) sy1 = sy0 + 1;
    if (sy1 > im.showH) sy1 = im.showH;
    for (int dx = 0; dx < dw; dx++) {
      const int px = cx0 + dx;
      if (px < x0 || px >= x0 + S) continue;
      int sx0 = (int)(dx * inv), sx1 = (int)((dx + 1) * inv);
      if (sx1 <= sx0) sx1 = sx0 + 1;
      if (sx1 > im.showW) sx1 = im.showW;
      int sum = 0, cnt = 0;
      for (int y = sy0; y < sy1; y++) for (int x = sx0; x < sx1; x++) { sum += imgNib(im, bpr, x, y); cnt++; }
      const uint8_t a = cnt ? (sum + cnt / 2) / cnt : 0;
      if (a) gfx->drawPixel(px, py, greyColor(a, 0xFF));
    }
  }
}

// v7.28.6 RGB565 彩色封面按 cover 方式铺满 S×S（最近邻采样、居中裁切）
void drawColorArt(int x0, int y0, int S) {
  const int sw = artColor.w, sh = artColor.h;
  if (!artColor.show || sw == 0 || sh == 0) return;
  const float s = fmaxf((float)S / sw, (float)S / sh);
  const int dw = (int)(sw * s), dh = (int)(sh * s);
  const int cx0 = x0 - (dw - S) / 2, cy0 = y0 - (dh - S) / 2;
  const float inv = 1.0f / s;
  for (int dy = 0; dy < dh; dy++) {
    const int py = cy0 + dy;
    if (py < y0 || py >= y0 + S) continue;
    int sy = (int)(dy * inv); if (sy >= sh) sy = sh - 1;
    const uint16_t *row = artColor.show + (size_t)sy * sw;
    for (int dx = 0; dx < dw; dx++) {
      const int px = cx0 + dx;
      if (px < x0 || px >= x0 + S) continue;
      int sx = (int)(dx * inv); if (sx >= sw) sx = sw - 1;
      gfx->drawPixel(px, py, row[sx]);
    }
  }
}

// 彩色封面的平均色（粗采样）：给背景压暗层用，让背景与封面同色调
static uint16_t artAverageColor() {
  if (!artColor.show || artColor.w == 0 || artColor.h == 0) return 0;
  uint32_t r = 0, g = 0, b = 0; int n = 0;
  const int stW = max(1, artColor.w / 12), stH = max(1, artColor.h / 12);
  for (int y = 0; y < artColor.h; y += stH)
    for (int x = 0; x < artColor.w; x += stW) {
      const uint16_t c = artColor.show[y * artColor.w + x];
      r += (c >> 8) & 0xF8; g += (c >> 3) & 0xFC; b += (c << 3) & 0xF8; n++;
    }
  if (!n) return 0;
  r /= n; g /= n; b /= n;
  return ((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (uint16_t)(b >> 3);
}

// 生成式封面：主色渐变方块 + 白色音符 + 细边框（App 还没推真封面时的占位）
void drawPlaceholderCover(uint16_t base) {
  const int x0 = COVER_X0, y0 = COVER_Y0, S = COVER_SZ;
  for (int i = 0; i < S; i++)
    gfx->drawFastHLine(x0, y0 + i, S, dimC(base, 32 - i * 18 / S));   // 上亮下暗
  const int cx = CX, cy = y0 + S / 2;
  gfx->fillRect(cx - 6, cy - 32, 7, 52, C_WHITE);                     // 音符竖杆
  gfx->fillTriangle(cx - 6, cy - 32, cx + 30, cy - 14, cx - 6, cy - 10, C_WHITE);  // 旗
  for (float r = 0; r < 26; r += 0.8f)                                // 音符头（实心圆）
    gfx->drawFastHLine(cx - 26 + (int)r, cy + 8 + (int)sqrtf(r * (52 - r)) - 10, 1, C_WHITE),
    gfx->drawFastHLine(cx - 26 + (int)r, cy + 12 - (int)sqrtf(r * (52 - r)) + 10, 1, C_WHITE);
  for (int i = 0; i < S; i++) {                                       // 1px 边框
    gfx->drawPixel(x0 + i, y0, dimC(base, 26)); gfx->drawPixel(x0 + i, y0 + S - 1, dimC(base, 26));
    gfx->drawPixel(x0, y0 + i, dimC(base, 26)); gfx->drawPixel(x0 + S - 1, y0 + i, dimC(base, 26));
  }
}

// 三个控制按键：固定在屏幕下方 y=390（v7.28.6 上移 10px：外圈按键
// (±100,390) 距圆心 186+43=229 < 233，整圆收进圆屏内），播放中不再隐藏。
void drawControls() {
  bool linked = bleConnected;
  for (int i = 0; i < 3; i++) {
    int x = CX + (i - 1) * 100, y = 390;
    bool enabled = hidMusicMode ? (hidMediaReady() || appleMedia.controlReady(i)) : linked && musicHave;
    uint16_t c = enabled ? C_WHITE : C_GREY;
    aaDisc(x, y, 43, enabled && musicPressed == i ? 0x0470 : enabled ? 0x19A7 : 0x1082);
    if (i == 1) {
      if (hidMusicMode && !appleMedia.playback.known) {
        gfx->fillTriangle(x - 19, y - 15, x - 19, y + 15, x + 1, y, c);
        gfx->fillRect(x + 7, y - 15, 5, 30, c); gfx->fillRect(x + 16, y - 15, 5, 30, c);
      } else if (musicPlaying) {
        gfx->fillRect(x - 13, y - 15, 8, 30, c);
        gfx->fillRect(x + 5, y - 15, 8, 30, c);
      } else gfx->fillTriangle(x - 10, y - 17, x - 10, y + 17, x + 17, y, c);
    } else {
      int sign = i == 0 ? -1 : 1;
      gfx->fillTriangle(x - sign * 12, y - 15, x - sign * 12, y + 15, x + sign * 10, y, c);
      gfx->fillRect(x + sign * 15 - 3, y - 15, 6, 30, c);
    }
  }
}

// 按键操作反馈文案（musicAck 1..11）
static const char *musicFeedback[] = {"", "SENT", "PHONE RECEIVED", "DONE", "NO RESPONSE", "NO PLAYER", "HID SENT", "HID NOT READY", "HID SEND FAILED", "AMS SENT", "AMS SEND FAILED", "AMS QUEUED"};

// v7.28.5 音乐页：上半部分方块歌曲封面（背景为封面主色压暗），封面下面
// 歌名 + 时长；点击歌名显示歌词（≤3 行，手机渲染的位图）；三个控制按键
// 固定在屏幕下方常驻显示。
void drawMusic() {
  gfx->fillScreen(C_BLACK);
  onWaitScreen = false;
  frameIsMap = false;
  bool linked = bleConnected;                  // v7.19 iOS 连上即视为已连接，不再要求加密验证
  bool playing = linked && musicHave && musicPlaying;
  if (appleMedia.available) playing = linked && appleMedia.playback.known && appleMedia.playback.state == 1;

  // 背景/封面主色：彩色封面取其平均色，否则按歌名+专辑哈希生成
  const uint16_t base = artColor.w ? artAverageColor() : musicCoverColor();
  gfx->fillScreen(dimC(base, 9));              // 背景：封面主色约 28% 亮度
  if (artColor.w) drawColorArt(COVER_X0, COVER_Y0, COVER_SZ);
  else if (imgs[13].showW > 0) drawCoverArt(13, COVER_X0, COVER_Y0, COVER_SZ);
  else drawPlaceholderCover(base);

  if (playing || (linked && musicHave)) {
    if (imgs[5].showW > 0) drawTextImg(5, TITLE_Y, 0xFF);           // 歌名（App 渲染的文字图，槽位5）
    else if (appleMedia.available && appleMedia.values[0].text.length()) {
      String t = appleMedia.values[0].text;
      if (t.length() > 22) t = t.substring(0, 21) + "…";
      printCentered(t, CX, TITLE_Y + 6, 2, C_WHITE);
    }
    if (appleMedia.available && appleMedia.playback.known) {
      double duration = AppleMediaData::duration(appleMedia.values[3].text.c_str());
      if (duration > 0) {
        double elapsed = appleMedia.playback.elapsed + appleMedia.playback.rate * (millis() - appleMedia.playbackAt) / 1000.0;
        int sec = (int)constrain(elapsed, 0.0, duration), total = (int)duration;
        char time[32]; snprintf(time, sizeof(time), "%d:%02d / %d:%02d", sec/60, sec%60, total/60, total%60);
        printCentered(time, CX, TIME_Y, 2, C_GREY);                 // 时长/进度
      }
    }
    if (musicAck > 0 && musicAck <= 11)
      printCentered(musicFeedback[musicAck], CX, TIME_Y + 30, 2, musicAck == 4 ? C_ORANGE : C_GREY);  // 按键反馈（临时占歌词行）
    else if (musicLyrics && imgs[12].showW > 0) drawTextImg(12, LYRIC_Y, 0xDD);   // 歌词（≤3 行）
    drawControls();
    needFlush = true;
    return;
  }
  if (hidMusicMode && linked && !appleMedia.available) {
    printCentered("PHONE MEDIA KEYS", CX, 244, 1, C_WHITE);
    printCentered("OPEN MUSIC APP ON PHONE", CX, 262, 1, C_GREY);
  } else if (!linked) printCentered("PHONE OFFLINE", CX, 248, 1, C_GREY);
  String state = linked && musicHave ? (musicPlaying ? "PLAYING" : "PAUSED") : "NO PLAYER";
  if (hidMusicMode) {
    state = !hidMediaReady() ? (appleMedia.controlMask() ? "AMS MEDIA READY" : "PAIR HID IN PHONE SETTINGS") : !appleMedia.available ? "HID READY / AMS UNAVAILABLE"
      : !appleMedia.playback.known ? "STATE UNKNOWN" : appleMedia.playback.state == 1 ? "PLAYING" : appleMedia.playback.state == 0 ? "PAUSED" : "SEEKING";
  }
  if (musicAck > 0 && musicAck <= 11) state = musicFeedback[musicAck];
  bool timeShown = false;
  if (appleMedia.available) {
    double duration = AppleMediaData::duration(appleMedia.values[3].text.c_str());
    if (duration > 0 && appleMedia.playback.known) {
      double elapsed = appleMedia.playback.elapsed + appleMedia.playback.rate * (millis() - appleMedia.playbackAt) / 1000.0;
      int sec = (int)constrain(elapsed, 0.0, duration), total = (int)duration;
      char time[32]; snprintf(time, sizeof(time), "%d:%02d / %d:%02d", sec/60, sec%60, total/60, total%60);
      state += String("  ") + time; timeShown = true;
    }
  }
  printCentered(state, CX, 296, 2, musicAck == 4 ? C_ORANGE : C_GREY);
  if (!timeShown) printCentered("SWIPE LEFT TO RETURN", CX, 330, 1, C_GREY);
  drawControls();
  drawPageDots();
  needFlush = true;
}

// 拖动状态
bool dragging = false;
int dragTarget = 0, dragSign = 1;
int dragWeatherDetail = 0;
bool dragHoriz = true;
float dragP = 0, dragV = 0;                 // 新页面已经进来多少像素；速度（像素/毫秒）
unsigned long dragT = 0;
int slideComposeP = -1;                     // 上一次合成用的 p（相同就不重复推屏）
bool slidePushPending = false;              // 合成好了但还没发出去（推屏忙时留到下一拍）

uint16_t *shownFrame() {
  if (fbBuf[1] && flushSrc) return flushSrc;
  return gfx->getFramebuffer();
}

static inline uint16_t dim565(uint16_t c, uint32_t k) {   // k: 0~32，32 = 原样
  uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  return (uint16_t)(((r * k >> 5) << 11) | ((g * k >> 5) << 5) | (b * k >> 5));
}

// 画一个页面到画布（不改当前页）
void renderPage(int p, int detail) {
  int keep = page;
  int keepDetail = wxDetail;
  bool ks = sliding;
  page = p; sliding = false;
  if (p == 2) drawMusic();
  else if (p == 1) { wxDetail = detail; drawWeather(); }
  else if (inNav) drawNavFrame();
  else drawWaiting(bleConnected ? WAIT_READY : WAIT_NOLINK);
  page = keep; wxDetail = keepDetail; sliding = ks;
}

// 准备滑动：存下屏幕上的旧画面，在画布上画好目标页面存起来
bool slidePrepare(int target, int detail) {
  const size_t BYTES = (size_t)LCD_WIDTH * LCD_HEIGHT * 2;
  if (!slideOld) slideOld = (uint16_t *)heap_caps_malloc(BYTES, MALLOC_CAP_SPIRAM);
  if (!slideNew) slideNew = (uint16_t *)heap_caps_malloc(BYTES, MALLOC_CAP_SPIRAM);
  if (!slideOld || !slideNew) return false;
  if (!chordReady) {
    for (int y = 0; y < LCD_HEIGHT; y++) {
      float d = y + 0.5f - LCD_HEIGHT / 2.0f, r = LCD_WIDTH / 2.0f + 1;
      chordHalf[y] = d * d < r * r ? (int16_t)ceilf(sqrtf(r * r - d * d)) : 0;
    }
    chordReady = true;
  }
  if (fbBuf[1]) { flushWait(); xSemaphoreGive(flushDone); }   // 等正在推的那帧推完，旧画面才完整
  memcpy(slideOld, shownFrame(), BYTES);
  // v7.28.4 目标页直接渲染进 slideNew（临时把画布指过去），省掉一次 466x466 整帧拷贝
  // —— 这一笔原来要 434KB PSRAM 往返，正好压在起手那一帧上，是「一碰就顿一下」的一部分。
  uint16_t *keepFb = gfx->getFramebuffer();
  gfx->setFramebuffer(slideNew);
  renderPage(target, detail);
  gfx->setFramebuffer(keepFb);
  pushNearest = true;                  // 拖动期间推屏走最近邻取样（画质静止时恢复）
  slideComposeP = -1; slidePushPending = false;
  sliding = true;
  needFlush = false; frameIsMap = false;
  return true;
}

// 合成一帧：新页面已经进来 p 像素（盖在上面），旧页面往同方向退 30%、变暗，交界处一道阴影
void slideCompose(int p) {
  const int W = LCD_WIDTH, H = LCD_HEIGHT;
  if (p < 0) p = 0;
  if (p > (dragHoriz ? W : H)) p = dragHoriz ? W : H;
  // 位置没变、上一帧也已经在传：整帧重算纯属浪费（手指按住不动时每 8ms 都会进来一次）
  if (p == slideComposeP && !slidePushPending) { needFlush = false; frameIsMap = false; return; }
  uint16_t *fb = gfx->getFramebuffer();
  int full = dragHoriz ? W : H;
  int q = p * 3 / 10;                                   // 旧页面退后的距离
  uint32_t k0 = 32 - (uint32_t)(p * 13 / full);         // 旧页面整体亮度：100% → 约 60%
  const int SH = 22;                                    // 阴影宽度
  if (dragHoriz) {
    static uint8_t colK[LCD_WIDTH];
    for (int x = 0; x < W; x++) {
      int dist = dragSign > 0 ? x - p : (W - p - 1) - x;   // 离交界多远（旧页面这一侧）
      uint32_t k = k0;
      if (p > 0 && dist < SH) k = k * (14 + dist * 18 / SH) / 32;
      colK[x] = (uint8_t)k;
    }
    for (int y = 0; y < H; y++) {
      int h = chordHalf[y];
      if (!h) continue;
      int xa = max(0, W / 2 - h), xb = min(W, W / 2 + h);
      uint16_t *row = fb + (size_t)y * W;
      const uint16_t *o = slideOld + (size_t)y * W, *n = slideNew + (size_t)y * W;
      if (dragSign > 0) {                               // 新页面从左边进来
        int a = xa, b = min(p, xb);
        if (b > a) memcpy(row + a, n + (W - p) + a, (b - a) * 2);
        for (int x = max(p, xa); x < xb; x++) row[x] = colK[x] >= 32 ? o[x - q] : dim565(o[x - q], colK[x]);
      } else {                                          // 新页面从右边进来
        int a = max(W - p, xa), b = xb;
        if (b > a) memcpy(row + a, n + (a - (W - p)), (b - a) * 2);
        for (int x = xa; x < min(W - p, xb); x++) row[x] = colK[x] >= 32 ? o[x + q] : dim565(o[x + q], colK[x]);
      }
      // 新页面前沿一道细亮边，看得出是一张“卡片”盖过来
      if (p > 1 && p < W - 1) {
        int ex = dragSign > 0 ? p - 1 : W - p;
        if (ex >= xa && ex < xb) row[ex] = 0x4A69;
        int ex2 = ex + (dragSign > 0 ? -1 : 1);
        if (ex2 >= xa && ex2 < xb) row[ex2] = 0x2945;
      }
    }
  } else {
    for (int y = 0; y < H; y++) {
      int h = chordHalf[y];
      if (!h) continue;
      int xa = max(0, W / 2 - h), xb = min(W, W / 2 + h);
      uint16_t *row = fb + (size_t)y * W;
      bool isNew = dragSign > 0 ? y < p : y >= H - p;
      if (isNew) {
        int sy = dragSign > 0 ? y + (H - p) : y - (H - p);
        memcpy(row + xa, slideNew + (size_t)sy * W + xa, (xb - xa) * 2);
        int edge = dragSign > 0 ? p - 1 : H - p;                // 前沿细亮边
        if (p > 1 && p < H - 1 && (y == edge || y == edge + (dragSign > 0 ? -1 : 1))) {
          uint16_t ec = y == edge ? 0x4A69 : 0x2945;
          for (int x = xa; x < xb; x++) row[x] = ec;
        }
      } else {
        int sy = dragSign > 0 ? y - q : y + q;
        int dist = dragSign > 0 ? y - p : (H - p - 1) - y;
        uint32_t k = k0;
        if (p > 0 && dist < SH) k = k * (14 + dist * 18 / SH) / 32;
        const uint16_t *o = slideOld + (size_t)sy * W;
        for (int x = xa; x < xb; x++) row[x] = k >= 32 ? o[x] : dim565(o[x], k);
      }
    }
  }
  // v7.28.4 拖动期间不再等推屏：上一帧还在传就先放着，等下一次采样（8ms 后）再补发。
  // 原来这里调 flushAsync()，会一直等到上一帧 QSPI 传完，手指因此被推屏节奏卡住——
  // 这是「跟手但发涩」最直接的一条。
  if (p != slideComposeP) { slideComposeP = p; slidePushPending = true; }
  if (slidePushPending && flushAsyncDrop()) slidePushPending = false;
  needFlush = false; frameIsMap = false;
}

// 换页完成
void slideCommit() {
  const size_t BYTES = (size_t)LCD_WIDTH * LCD_HEIGHT * 2;
  if (fbBuf[1]) { flushWait(); xSemaphoreGive(flushDone); }   // 最后一帧拖动画面推完再换静止画面
  pushNearest = false; slideComposeP = -1; slidePushPending = false;
  sliding = false; dragging = false;
  page = dragTarget;
  if (page == 1) wxDetail = dragWeatherDetail;
  pageAt = millis();
  memcpy(gfx->getFramebuffer(), slideNew, BYTES);
  needFlush = true; frameIsMap = false;
  if (page == 2) {
    onWaitScreen = false; musicDirty = true; pendingEvt = "T,M,Q";
  } else if (page == 1) {
    onWaitScreen = false;
    wxDirty = false;
    if (!wxHave || millis() - wxAt > 15UL * 60 * 1000) { pendingEvt = "T,W"; wxAskAt = millis(); }
  } else {
    if (inNav) navDirty = true;
    else { catForce = true; }
  }
}

// 没换成：回到原页面
void slideCancel() {
  if (fbBuf[1]) { flushWait(); xSemaphoreGive(flushDone); }
  pushNearest = false; slideComposeP = -1; slidePushPending = false;
  sliding = false; dragging = false;
  renderPage(page, wxDetail);
  if (page == 2) musicDirty = true;
  else if (page == 1) wxDirty = false;
  else if (inNav) navDirty = true;
  else { onWaitScreen = true; catForce = true; }
  needFlush = true;
}

// 从 p0 顺势滑到 p1（先快后慢）；ms = 时长
void slideAnimate(float p0, float p1, int ms) {
  unsigned long t0 = millis();
  for (;;) {
    float t = (millis() - t0) / (float)ms;
    if (t >= 1) break;
    float e = 1 - (1 - t) * (1 - t) * (1 - t);         // ease-out
    slideCompose((int)(p0 + (p1 - p0) * e));
  }
  slideCompose((int)p1);
}

// 触摸向量统一换算：先修正芯片坐标的镜像，再按刷屏的逆旋转映射到画布。
// 位移和相对屏幕中心的点都使用这一个函数，避免旋转后点按与手势方向不一致。
void touchVectorToCanvas(float &dx, float &dy) {
  if (TOUCH_FLIP_X) dx = -dx;
  if (TOUCH_FLIP_Y) dy = -dy;
  float ox = dx, oy = dy;
  if (rotFree) { dx = ox * rotCosF + oy * rotSinF; dy = -ox * rotSinF + oy * rotCosF; }
  else if (screenRot == 1) { dx = oy; dy = -ox; }
  else if (screenRot == 2) { dx = -ox; dy = -oy; }
  else if (screenRot == 3) { dx = -oy; dy = ox; }
}

// 手指在拖：(dx, dy) = 从按下到现在的位移
void dragMove(int dx, int dy, unsigned long now) {
  float vx = dx, vy = dy;
  touchVectorToCanvas(vx, vy);
  dx = (int)lroundf(vx); dy = (int)lroundf(vy);
  if (!dragging) {
    // 所有页面用同一个阈值，与 touchPoll() 的「算不算点按」判据严格一致
    if (abs(dx) < DRAG_START && abs(dy) < DRAG_START) return;
    dragHoriz = abs(dx) >= abs(dy);
    int d = dragHoriz ? dx : dy;
    dragSign = d > 0 ? 1 : -1;
    const auto next = FixedPageLayout::move(page, wxDetail, dragHoriz, dragSign);
    if (!next.allowed) return;
    dragTarget = next.page; dragWeatherDetail = next.detail;
    if (!slidePrepare(dragTarget, dragWeatherDetail)) return;
    dragging = true;
    dragP = 0; dragV = 0; dragT = now;
  }
  float p = (dragHoriz ? dx : dy) * dragSign;
  if (p < 0) p = 0;
  float dt = now - dragT;
  if (dt > 0) dragV = dragV * 0.5f + ((p - dragP) / dt) * 0.5f;
  dragP = p; dragT = now;
  slideCompose((int)p);
}

// 松手：拖过 30% 或者往前甩出去 → 顺势滑完；否则弹回
void dragEnd() {
  if (!dragging) return;
  int full = dragHoriz ? LCD_WIDTH : LCD_HEIGHT;
  bool go = dragP > full * 0.3f || (dragV > 0.35f && dragP > 24);
  if (dragV < -0.35f) go = false;                       // 往回甩：取消
  float target = go ? full : 0;
  float left = fabsf(target - dragP);
  int ms = 140 + (int)(260 * left / full);
  if (go && dragV > 0.5f) ms = max(140, (int)(ms * 0.75f));   // 甩得快就滑得快
  slideAnimate(dragP, target, ms);
  if (go) slideCommit(); else slideCancel();
}

// 来电等情况：直接放弃拖动
void dragAbort() {
  if (!dragging) return;
  pushNearest = false; slideComposeP = -1; slidePushPending = false;
  sliding = false; dragging = false;
  if (page == 2) musicDirty = true; else if (page == 1) wxDirty = true; else if (inNav) navDirty = true;
}

// 将触摸点换算到画布坐标，按钮使用较大的点按范围。
int musicButtonAt(int x, int y) {
  float dx = x - CX, dy = y - CX;
  touchVectorToCanvas(dx, dy);
  x = lroundf(CX + dx); y = lroundf(CX + dy);
  for (int i = 0; i < 3; i++) {
    int bx = CX + (i - 1) * 100;
    if ((x - bx) * (x - bx) + (y - 390) * (y - 390) <= 49 * 49) return i;   // v7.28.6 按键上移到 y=390
  }
  // v7.16 1.43C 边缘点按容错：取最近按钮，距离 ≤58 也算命中（按钮间距 100，不会误触邻键）
  int best = -1; long bestD = 58L * 58;
  for (int i = 0; i < 3; i++) {
    int bx = CX + (i - 1) * 100;
    long d = (long)(x - bx) * (x - bx) + (long)(y - 390) * (y - 390);
    if (d <= bestD) { bestD = d; best = i; }
  }
  return best;
}

void musicTap(int x, int y) {
  if (dragging || page != 2 || callOn || pairOn || !bleConnected) return;
  int button = musicButtonAt(x, y);
  Serial.printf("[music] tap xy=(%d,%d) button=%d hidMode=%d hidReady=%d amsReady=%d mask=%d musicHave=%d\n",
                x, y, button, (int)hidMusicMode, (int)hidMediaReady(), (int)appleMedia.available,
                appleMedia.controlMask(), (int)musicHave);
  if (hidMusicMode) {
    // v7.28.5 点歌名/时长区域（封面下方一条带）切换歌词显示；其余空白处忽略
    if (button < 0 && y >= TITLE_Y && y <= LYRIC_Y + 56 && abs(x - CX) <= 132 && appleMedia.available) {
      musicLyrics = !musicLyrics;
      lyricImageGeneration = 0; imgs[12].showW = 0; imgs[12].showH = 0; imgs[12].recvId = -1; imgs[12].recvMask = 0;
      pendingLyricsStatus = String("ML,") + (musicLyrics ? 1 : 0);   // 同步给手机：开就推歌词图
      Serial.printf("MEDIA LYRICS toggle=%d (tap)\n", (int)musicLyrics);
      musicDirty = true;
      return;
    }
    if (button < 0) return;
    bool hidReady = hidMediaReady();
    if (!hidReady && !appleMedia.controlReady(button)) { musicAck = 7; musicAckUntil = millis() + 2500; musicDirty = true; return; }
    if (hidTapAt && millis() - hidTapAt < 120) return;
    hidTapAt = millis();
    if (!hidReady) {
      musicAck = appleMedia.control(button) ? 11 : 10; musicAckUntil = millis() + 2500; musicDirty = true; return;
    }
    if (!hidPulses.enqueue(button)) { musicAck = 8; musicAckUntil = millis() + 2500; musicDirty = true; }
    return; // no T,M,P/T/N: do not also control Apple Music through the App
  }
  if (button < 0 || !musicHave) {
    pendingEvt = String("T,M,H,") + x + "," + y + "," + button;
    return;
  }
  pendingEvt = String("T,M,") + (button == 0 ? "P" : button == 1 ? "T" : "N") + "," + x + "," + y + "," + button;
  musicAck = 1; musicAckUntil = millis() + 2500; musicDirty = true;
}

// 串口自检（不需要手指）：发 't' 打印 MADCTL 坐标变换与三个音乐按钮的命中结果，
// 用来回归「触摸映射 / 按钮定位」有没有被改坏。由 MotoNav.ino 的 loop() 调用。
void touchSelfTest() {
  Serial.println("[touch] selftest MADCTL=0x60: raw -> screen(360) -> logical(466)");
  const int16_t rx[4] = {0, 359, 0, 180};
  const int16_t ry[4] = {0, 0, 359, 180};
  for (int i = 0; i < 4; i++) {
    int16_t x = rx[i], y = ry[i];
    const int16_t sx = ry[i], sy = PANEL_HEIGHT - 1 - rx[i];
    panelPointToLogical(x, y);
    Serial.printf("[touch] raw=(%3d,%3d) scr=(%3d,%3d) log=(%3d,%3d)\n",
                  (int)rx[i], (int)ry[i], (int)sx, (int)sy, (int)x, (int)y);
  }
  for (int i = 0; i < 3; i++) {
    const int bx = CX + (i - 1) * 100;
    Serial.printf("[touch] music btn center (%3d,390) -> hit=%d (expect %d)\n",
                  bx, musicButtonAt(bx, 390), i);
  }
}

// 固定页面不因导航距离自动跳转；音乐回执照常刷新。
void musicTick() {
  if (musicAckUntil && (long)(millis() - musicAckUntil) >= 0) {
    if (musicAck == 1) { musicAck = 4; musicAckUntil = millis() + 2500; }
    else { musicAck = 0; musicAckUntil = 0; }
    musicDirty = true;
  }
  if (page != 2 || callOn || pairOn || sliding) return;
  if (musicDirty) { musicDirty = false; drawMusic(); }
}

// 天气列共享请求和数据；保持用户手动选中的页面。
void weatherTick() {
  if (page != 1 || callOn || pairOn || sliding) return;
  unsigned long now = millis();
  // 还没数据或数据旧了：30 秒再要一次
  if (bleConnected && (!wxHave || now - wxAt > 15UL * 60 * 1000) && now - wxAskAt > 30000) { pendingEvt = "T,W"; wxAskAt = now; }
  if (wxDirty) { wxDirty = false; drawWeather(); }
}

void drawStaleOverlay() {
  drawRedRing();
  printCentered("NO DATA", CX, 412, 3, C_RED);
  needFlush = true;
}
