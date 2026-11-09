#pragma once
// 触摸初始化与来电页面。由 MotoNav.ino 按依赖顺序包含，仅编译一次。
// ================= v5.1 来电显示 =================
// 手机来电时整屏显示：上面 “INCOMING CALL”，中间绿色电话图标带往外扩散的波纹，下面是名字（手机渲染好的
// 文字图片，图片栏 2）和号码；接听或挂断后回到原来的画面（地图 / 大箭头 / 首页）
bool callOn = false;
String callNum = "";
unsigned long callAt = 0, callSeenAt = 0;

// v5.2 触屏接电话：按住任意位置 HOLD_MS 毫秒 → 发 "T,A" 给手机（v6.9 800 → 200）
#define HOLD_MS 200
TouchDrvCST816 touch;                // 谷仓板 CST816（I2C 0x15，官方 sdgoods_lvgl.c 同地址）
bool touchOK = false;
unsigned long touchHoldAt = 0, touchLastSeen = 0;
float touchHold = 0;                 // 0~1 按住的进度
bool callAnswerSent = false;
unsigned long callAnswerAt = 0;
String pendingProf = "";             // v5.8 分步用时
String pendingEvt = "";              // 待发给手机的触摸事件

// v5.3 触摸芯片一有动静就拉低中断脚；平时只看这个标记，不用一直读 I2C
volatile bool touchIrq = false;
void IRAM_ATTR onTouchIrq() { touchIrq = true; }

void touchInit() {
  touch.setPins(TP_RESET, TP_INT);          // 官方 board_pins.h：RST GPIO5 / INT GPIO4
  touchOK = touch.begin(Wire, TOUCH_I2C_ADDR, IIC_SDA, IIC_SCL);
  Serial.println(touchOK ? "touch ok (CST816)" : "touch not found");
  if (touchOK) {
    touch.disableAutoSleep();               // 避免待机后触摸无响应
    attachInterrupt(TP_INT, onTouchIrq, FALLING);
  }
}

// 官方 sdgoods_tap.h 的 SDGOODS_TAP_SLOP = 24 物理 px（CST816 端点抖动实测 8~10px）。
// 逻辑画布 466 / 物理 360 ≈ 1.294 ⇒ 31 逻辑 px。
// 「点按允许的最大位移」与「开始起拖的阈值」必须是同一个数：官方 BSP 明确记录过，
// 两者不等时会出现「既被判成滑动、又被判成点按」的矛盾区间，点按会凭空消失、滑动抢走点击。
#define TOUCH_TAP_SLOP 31

// 官方 BSP components/sdgoods_board/src/sdgoods_input.c :: map_raw_to_lvgl()
//     x = LCD_WIDTH-1-raw_x ; y = raw_y ; out_x = y ; out_y = x
// MADCTL=0x60（MV=1 行列互换 + MX=1 X 镜像）让 CST816 原始坐标与屏幕显示坐标差一次
// 90° 旋转：screen_x = raw_y，screen_y = PANEL_HEIGHT-1-raw_x。
// 漏掉这一步 = X/Y 互换且 X 倒置，正是「音乐按钮点不中、横滑被当成竖滑」的根因。
// 之后再把 360 物理坐标线性放大到 466 逻辑画布（与 PortScale::pixel 的取样互逆）；
// 软件自由旋转由 PageNavigation.h 的 touchVectorToCanvas() 继续处理。
static inline void panelPointToLogical(int16_t &x, int16_t &y) {
  int16_t rx = x, ry = y;
  if (rx < 0) rx = 0;
  if (ry < 0) ry = 0;
  if (rx >= PANEL_WIDTH)  rx = PANEL_WIDTH - 1;
  if (ry >= PANEL_HEIGHT) ry = PANEL_HEIGHT - 1;
  const int16_t sx = ry;
  const int16_t sy = PANEL_HEIGHT - 1 - rx;
  x = PortScale::touch(sx);
  y = PortScale::touch(sy);
}

bool touchDown = false;
int16_t touchX0 = 0, touchY0 = 0, touchX1 = 0, touchY1 = 0;
void dragMove(int dx, int dy, unsigned long now);   // v5.5 跟手换页
int musicButtonAt(int x, int y);
void musicTap(int x, int y);
void dragEnd();
void dragAbort();

// 读触摸：按下 → 报坐标；来电时按住够久 → 接听；拖动 → 跟手换页（v5.5）
void touchPoll() {
  static unsigned long lastPoll = 0;
  static bool filtInit = false;
  static int16_t filtX = 0, filtY = 0;
  unsigned long now = millis();
  if (!touchOK) return;
  // 手势进行中无论中断是否再来都持续读；空闲时每 30ms 兜底轮询一次，
  // 防止 CST816 下降沿中断偶尔丢失造成「按下了却没反应」。
  if (!touchIrq && !touchDown && !callOn && now - lastPoll < 30) return;
  if (now - lastPoll < 8) return;
  lastPoll = now;
  touchIrq = false;
  if (callAnswerSent && now - callAnswerAt > 4000) callAnswerSent = false;   // 4 秒还在响：允许再按一次
  int16_t x[5] = {0}, y[5] = {0};
  uint8_t n = touch.getPoint(x, y, 1);
  if (n > 0) {
    const int16_t rawX = x[0], rawY = y[0];
    panelPointToLogical(x[0], y[0]);
    // CST816 端点抖动实测 8~10 物理 px：一阶低通压掉一半，划动更跟手，
    // 8ms 采样下只多约一帧延迟（首帧不滤波，保证起手坐标准确）。
    if (!filtInit) { filtX = x[0]; filtY = y[0]; filtInit = true; }
    else { filtX = (int16_t)((filtX + x[0]) >> 1); filtY = (int16_t)((filtY + y[0]) >> 1); }
    x[0] = filtX; y[0] = filtY;
    if (!touchDown) {
      touchDown = true;
      touchHoldAt = now;
      touchX0 = touchX1 = x[0]; touchY0 = touchY1 = y[0];
      pendingEvt = "T,P," + String(x[0]) + "," + String(y[0]);
      if (page == 2 && !callOn && !pairOn) { musicPressed = musicButtonAt(x[0], y[0]); musicDirty = true; }
      if (fxOn && !callOn) { stopArriveFx(); touchEat = true; }          // v7.1 点一下结束到达特效
      // 一次按下只打一行，方便实机核对「手指位置 ↔ 屏幕位置」是否一致
      Serial.printf("[touch] down raw=(%d,%d) logical=(%d,%d) btn=%d\n",
                    (int)rawX, (int)rawY, (int)x[0], (int)y[0],
                    page == 2 ? musicButtonAt(x[0], y[0]) : -99);
    }
    touchX1 = x[0]; touchY1 = y[0];
    touchLastSeen = now;
    if (callOn || pairOn || touchEat || fxOn) dragAbort();
    else dragMove(touchX1 - touchX0, touchY1 - touchY0, now);
  } else {
    filtInit = false;                       // 松手后下一次按下重新起滤波
    // CST816 偶尔会丢一帧（含 disableAutoSleep 后的 0xFF 帧）；120ms 宽限避免
    // 划动中途被误判成松手、页面突然弹回。
    if (touchDown && now - touchLastSeen > 120) {
      touchDown = false;
      touchHoldAt = 0;
      if (callOn || pairOn || touchEat || fxOn) dragAbort();
      else {
        if (page == 2 && abs(touchX1 - touchX0) < TOUCH_TAP_SLOP &&
            abs(touchY1 - touchY0) < TOUCH_TAP_SLOP) musicTap(touchX0, touchY0);
        if (page == 1 && abs(touchX1 - touchX0) < TOUCH_TAP_SLOP &&
            abs(touchY1 - touchY0) < TOUCH_TAP_SLOP) weatherTap();
        dragEnd();
      }
      touchEat = false;
      if (musicPressed >= 0) { musicPressed = -1; musicDirty = true; }
    }
  }
  // 来电：按住不动才算（滑动不算）
  bool still = abs(touchX1 - touchX0) < 60 && abs(touchY1 - touchY0) < 60;
  touchHold = (callOn && touchDown && still) ? min(1.0f, (now - touchHoldAt) / (float)HOLD_MS) : 0;
  if (touchHold >= 1 && !callAnswerSent) {
    callAnswerSent = true;
    callAnswerAt = now;
    touchHold = 0;
    pendingEvt = "T,A";
  }
}

// 电话听筒：一段拱形的圆点串当手柄，两头各一个大点当听筒和话筒，整体斜过来（常见的电话图标）
void drawHandset(float cx, float cy, float r, uint16_t c) {
  const float ROT = -35 * PI / 180, cr = cosf(ROT), sr = sinf(ROT);
  auto dot = [&](float x, float y, float rr) {
    aaDisc(cx + x * cr - y * sr, cy + x * sr + y * cr, rr, c);
  };
  for (int a = 25; a <= 155; a += 5) {
    float rad = a * PI / 180;
    dot(cosf(rad) * r, -sinf(rad) * r * 0.55f + r * 0.25f, r * 0.22f);
  }
  for (int k = 0; k < 2; k++) {
    float rad = (k ? 155 : 25) * PI / 180;
    dot(cosf(rad) * r + (k ? -1 : 1) * r * 0.04f, -sinf(rad) * r * 0.55f + r * 0.25f + r * 0.21f, r * 0.32f);
  }
}

void drawCallScreen() {
  onWaitScreen = false;
  frameIsMap = false;
  gfx->fillScreen(C_BLACK);
  unsigned long t = millis() - callAt;
  const float icy = 168;
  // 往外扩散的两圈波纹
  for (int k = 0; k < 2; k++) {
    float ph = fmodf(t / 1400.0f + k * 0.5f, 1.0f);
    float r = 52 + ph * 110;
    uint16_t c = blend565(C_GREEN, C_BLACK, (int)(ph * 255));
    for (int w = 0; w < 3; w++) gfx->drawCircle(CX, (int)icy, (int)r + w, c);
  }
  aaDisc(CX, icy, 50, 0x2DC6);
  aaDisc(CX, icy, 46, C_GREEN);
  // 听筒轻轻左右晃
  float wob = ((t / 120) % 4 < 2) ? -2.5f : 2.5f;
  drawHandset(CX + wob, icy, 26, C_WHITE);
  printCentered("INCOMING CALL", CX, 58, 2, C_GREEN);
  bool hasName = imgs[2].show && imgs[2].showW > 0;
  if (hasName) {
    drawTextImg(2, 252, 0xFF);
    if (callNum.length()) printCentered(callNum, CX, 318, 2, C_GREY);
  } else if (callNum.length()) {
    printCentered(callNum, CX, 262, callNum.length() > 12 ? 3 : 4, C_WHITE);
  } else {
    printCentered("UNKNOWN", CX, 262, 4, C_WHITE);
  }
  // v5.2 触屏接听提示；按住时绿圈外面一圈白点从正上方顺时针走满
  if (touchOK) {
    if (callAnswerSent) printCentered("ANSWERING...", CX, 392, 2, C_GREEN);
    else printCentered("TAP & HOLD", CX, 392, 2, C_GREY);      // v6.9
    if (touchHold > 0 && !callAnswerSent) {
      int steps = (int)(touchHold * 72);
      for (int k = 0; k <= steps; k++) {
        float a = k * 5 * PI / 180;
        aaDisc(CX + sinf(a) * 64, icy - cosf(a) * 64, 4, C_WHITE);
      }
    }
  }
  needFlush = true;
}
