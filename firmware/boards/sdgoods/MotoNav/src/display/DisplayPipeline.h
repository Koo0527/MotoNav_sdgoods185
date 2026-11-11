#pragma once
// 双缓冲、旋转和 QSPI 推屏。由 MotoNav.ino 按依赖顺序包含，仅编译一次。
// 谷仓次元屏：逻辑画布 466x466 -> 物理面板 ST77916 360x360，
// 每个物理像素由 PortScale 双线性取样，旋转也在取样阶段完成。
// ---- v2.2 双缓冲：核心 0 上的刷屏任务 ----
uint16_t *fbBuf[2] = {nullptr, nullptr};
int fbCur = 0;                    // 画布当前用的是哪一块
SemaphoreHandle_t flushReq = nullptr, flushDone = nullptr;
uint16_t *volatile flushSrc = nullptr;
volatile uint32_t flushMsAcc = 0, flushCnt = 0;
// v7.28.4 跟手拖动期间置 true：推屏改用最近邻取样，省掉双线性插值的 3/4 次 PSRAM 读。
// 只在 slidePrepare ~ slideCommit 之间生效，静止画面仍旧走双线性，画质不变。
volatile bool pushNearest = false;

// ---- v2.6 快速刷屏：自己往屏幕数据线上排队发送，两块缓冲轮流用（一块在传、一块在准备）----
#define FAST_PIX 4096                     // 每段像素数（8KB）
spi_device_handle_t fastDev = nullptr;
uint8_t *fastBuf[2] = {nullptr, nullptr};

int screenRot = 0;                        // v6.2 圆屏方向：0 / 1 / 2 / 3 = 0° / 90° / 180° / 270°
int screenDeg = 0;                        // v6.4 圆屏方向（度，0~359）
bool rotFree = false;                     // v6.4 不是 90° 的整数倍：推屏时逐像素按角度取样
int32_t rotCos = 65536, rotSin = 0;       // v6.4 角度的 cos / sin（×65536）
float rotCosF = 1, rotSinF = 0;
unsigned long rotSaveAt = 0;              // v6.4 角度停下后过一会儿再存进板子
int16_t rotSpan[LCD_HEIGHT];              // v6.4 每一行圆里面的半宽（圆外面的像素直接涂黑）

// v6.4 设定圆屏角度。save = true：稍后存进板子
void setScreenDeg(int d, bool save) {
  d = ((d % 360) + 360) % 360;
  screenDeg = d;
  rotFree = (d % 90) != 0;
  screenRot = rotFree ? 0 : d / 90;
  float a = d * (float)M_PI / 180.0f;
  rotCosF = cosf(a); rotSinF = sinf(a);
  rotCos = (int32_t)lroundf(rotCosF * 65536.0f);
  rotSin = (int32_t)lroundf(rotSinF * 65536.0f);
  if (save) rotSaveAt = millis() + 1500;
}

void initRotSpan() {
  const int c = LCD_WIDTH / 2;
  for (int y = 0; y < LCD_HEIGHT; y++) {
    int dy = y - c;
    int r2 = c * c - dy * dy;
    rotSpan[y] = r2 > 0 ? (int16_t)sqrtf((float)r2) : 0;
  }
}

// v6.4 停下 1.5 秒后把角度存起来（loop 里调用）
void saveRotIfDue() {
  if (!rotSaveAt || (long)(millis() - rotSaveAt) < 0) return;
  rotSaveAt = 0;
  Preferences pr;
  pr.begin("motonav", false);
  pr.putInt("rotd", screenDeg);
  pr.putInt("rot", screenRot);
  pr.end();
}

bool fastFlushInit() {
  spi_device_interface_config_t dc;
  memset(&dc, 0, sizeof(dc));
  dc.command_bits = 8;
  dc.address_bits = 24;
  dc.mode = 0;
  dc.clock_source = SPI_CLK_SRC_DEFAULT;
  dc.clock_speed_hz = qspiMHz * 1000000;
  dc.spics_io_num = -1;                   // 片选自己控制（和屏幕库一样）
  dc.flags = SPI_DEVICE_HALFDUPLEX;
  dc.queue_size = 2;
  if (spi_bus_add_device(SPI2_HOST, &dc, &fastDev) != ESP_OK) { fastDev = nullptr; return false; }
  for (int i = 0; i < 2; i++) {
    fastBuf[i] = (uint8_t *)heap_caps_aligned_alloc(16, FAST_PIX * 2, MALLOC_CAP_DMA);
    if (!fastBuf[i]) { fastDev = nullptr; return false; }
  }
  return true;
}

// v7.7 RGB565 拆成 0x0GGRRBB 形式（绿、红、蓝之间留空），一次乘法就能三个颜色一起按权重混合
static inline uint32_t rgbSpread(uint16_t c) { return ((uint32_t)c | ((uint32_t)c << 16)) & 0x07E0F81Fu; }

// 快速路径：整屏 360x360 逐像素从 466x466 逻辑画布取样后，QSPI 连续写 GRAM。
// ST77916 的 QSPI 写显存帧 = cmd 0x32 + addr 0x002C00 (RAMWR)；
// CO5300(143C) 用的是 0x003C00，两者不可混用。
void __attribute__((optimize("O3"))) fastPush(const uint16_t *fb) {
  panel->startWrite();
  panel->writeAddrWindow(0, 0, PANEL_WIDTH, PANEL_HEIGHT);
  panel->endWrite();
  spi_device_acquire_bus(fastDev, portMAX_DELAY);
  digitalWrite(LCD_CS, LOW);
  static spi_transaction_ext_t tr[2];
  const uint32_t total = (uint32_t)PANEL_WIDTH * PANEL_HEIGHT;
  const bool nearest = pushNearest;                   // 拖动中：走最近邻，取样成本降到 1/4
  uint32_t sent = 0;
  int bi = 0, inflight = 0;
  bool first = true;
  while (sent < total) {
    uint32_t count = total - sent;
    if (count > FAST_PIX) count = FAST_PIX;
    if (inflight == 2) {                              // 两块都在传：等最早那块传完再复用
      spi_transaction_t *r;
      spi_device_get_trans_result(fastDev, &r, portMAX_DELAY);
      inflight--;
    }
    uint16_t *out = (uint16_t *)fastBuf[bi];
    int py = sent / PANEL_WIDTH, px = sent - py * PANEL_WIDTH;
    for (uint32_t i = 0; i < count; i++) {
      uint16_t v = nearest
        ? portScale.pixelNearest(fb, px, py, screenRot, rotFree, rotCos, rotSin)
        : portScale.pixel(fb, px, py, screenRot, rotFree, rotCos, rotSin);
      out[i] = (uint16_t)((v >> 8) | (v << 8));      // 屏幕要高字节在前
      if (++px == PANEL_WIDTH) { px = 0; py++; }
    }
    spi_transaction_ext_t &t = tr[bi];
    memset(&t, 0, sizeof(t));
    if (first) {
      t.base.flags = SPI_TRANS_MODE_QIO;
      t.base.cmd = 0x32;
      t.base.addr = ST77916_QSPI_RAMWR_ADDR;
      first = false;
    } else {
      t.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD |
                     SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
    }
    t.base.tx_buffer = fastBuf[bi];
    t.base.length = count * 16;
    spi_device_queue_trans(fastDev, (spi_transaction_t *)&t, portMAX_DELAY);
    inflight++;
    bi ^= 1;
    sent += count;
  }
  while (inflight) {
    spi_transaction_t *r;
    spi_device_get_trans_result(fastDev, &r, portMAX_DELAY);
    inflight--;
  }
  digitalWrite(LCD_CS, HIGH);
  spi_device_release_bus(fastDev);
}

// 标准路径：同样先缩放，再交给 GFX 的 draw16bitRGBBitmap 推 360x360。
void flushLogicalFrame(const uint16_t *fb) {
  if (!fb) return;
  if (fastDev) { fastPush(fb); return; }
  static uint16_t *physical = nullptr;
  if (!physical) physical = (uint16_t *)ps_malloc(PANEL_WIDTH * PANEL_HEIGHT * 2);
  const bool nearest = pushNearest;
  if (physical) {
    for (int y = 0; y < PANEL_HEIGHT; y++)
      for (int x = 0; x < PANEL_WIDTH; x++)
        physical[y * PANEL_WIDTH + x] = nearest
          ? portScale.pixelNearest(fb, x, y, screenRot, rotFree, rotCos, rotSin)
          : portScale.pixel(fb, x, y, screenRot, rotFree, rotCos, rotSin);
    panel->draw16bitRGBBitmap(0, 0, physical, PANEL_WIDTH, PANEL_HEIGHT);
  } else {
    uint16_t row[PANEL_WIDTH];
    for (int y = 0; y < PANEL_HEIGHT; y++) {
      for (int x = 0; x < PANEL_WIDTH; x++)
        row[x] = nearest
          ? portScale.pixelNearest(fb, x, y, screenRot, rotFree, rotCos, rotSin)
          : portScale.pixel(fb, x, y, screenRot, rotFree, rotCos, rotSin);
      panel->draw16bitRGBBitmap(0, y, row, PANEL_WIDTH, 1);
    }
  }
}

void flushTask(void *arg) {
  for (;;) {
    xSemaphoreTake(flushReq, portMAX_DELAY);
    uint32_t t0 = millis();
    flushLogicalFrame(flushSrc);
    flushMsAcc += millis() - t0; flushCnt++;
    xSemaphoreGive(flushDone);
  }
}

// v2.6 等刷屏完成；超过 3 秒还没完成，说明快速刷屏在这块板子上不工作：关掉它并重启
void flushWait() {
  if (xSemaphoreTake(flushDone, pdMS_TO_TICKS(3000)) == pdTRUE) return;
  if (fastFlushOn) saveAndRestart("fastf", 0, true);
  xSemaphoreTake(flushDone, portMAX_DELAY);
}

// 异步刷屏：把刚画好的这一块交给核心 0 推到屏幕，画布换到另一块继续画下一帧
void flushAsync() {
  if (!fbBuf[1]) { gfx->flush(); return; }
  flushWait();                                    // 等上一帧推完
  flushSrc = fbBuf[fbCur];
  xSemaphoreGive(flushReq);
  fbCur ^= 1;
  gfx->setFramebuffer(fbBuf[fbCur]);
}

// v7.28.4 拖动专用：上一帧还在推就先不发（返回 false），绝不让触摸任务等推屏。
//
// 为什么可以推迟：合成只写 fbBuf[fbCur]，正在推的一直是 fbBuf[fbCur^1]（flushAsync
// 在投递的同时就把 fbCur 翻过去了），所以晚一拍发不会覆盖正在传的缓冲；
// slideCompose 每帧都是从 slideOld/slideNew 整帧重算，也不存在增量丢失。
//
// 收益：手指位置不再被 QSPI 传输时间卡住——等一帧推完（十几毫秒）再采下一个点，
// 正是「划动跟手但发涩」的直接原因。返回值给调用方记账，下一拍接着补发。
bool flushAsyncDrop() {
  if (!fbBuf[1]) { gfx->flush(); return true; }
  if (xSemaphoreTake(flushDone, 0) != pdTRUE) return false;   // 上一帧还在传：这一帧不要了
  flushSrc = fbBuf[fbCur];
  xSemaphoreGive(flushReq);
  fbCur ^= 1;
  gfx->setFramebuffer(fbBuf[fbCur]);
  return true;
}

// 同步刷屏（普通画面用，等推完再返回）
void flushSync() {
  if (!fbBuf[1]) { gfx->flush(); return; }
  flushWait();
  flushSrc = fbBuf[fbCur];
  xSemaphoreGive(flushReq);
  flushWait();                                    // 等这一帧推完
  xSemaphoreGive(flushDone);
}

void initDoubleBuffer() {
  fbBuf[0] = gfx->getFramebuffer();
  fbBuf[1] = (uint16_t *)heap_caps_aligned_alloc(16, LCD_WIDTH * LCD_HEIGHT * 2, MALLOC_CAP_SPIRAM);
  if (!fbBuf[1]) return;
  memcpy(fbBuf[1], fbBuf[0], LCD_WIDTH * LCD_HEIGHT * 2);
  if (fastFlushOn) fastFlushInit();                // v2.6 快速刷屏
  flushReq = xSemaphoreCreateBinary();
  flushDone = xSemaphoreCreateBinary();
  xSemaphoreGive(flushDone);
  xTaskCreatePinnedToCore(flushTask, "flush", 4096, nullptr, 2, nullptr, 0);
}
