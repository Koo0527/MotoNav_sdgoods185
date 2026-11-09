// MotoNav 1.75：程序入口与运行调度。
// v7.12.9-junction.10：保留独立卡片框架；缺少右转数据改为灰色箭头和灰色实心圆。
// 文件职责、依赖和验证结果见工程目录的“源码结构说明.txt”。
// 实现头文件只由此入口包含，保持原编译顺序和内联优化。
#include "src/core/FirmwareConfig.h"
#include <Arduino.h>
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "src/core/DeviceContext.h"
#include "src/core/FunctionDeclarations.h"
#include "src/connectivity/Bluetooth.h"
#include "src/display/DisplayPipeline.h"
#include "src/graphics/VectorDrawing.h"
#include "src/graphics/ProgressRings.h"
#include "src/power/Battery.h"
#include "src/graphics/StatusWidgets.h"
#include "src/graphics/LaneDrawing.h"
#include "src/graphics/TextBitmaps.h"
#include "src/protocol/TextBitmapReceiver.h"
#include "src/pages/HomePage.h"
#include "src/pages/PairingPage.h"
#include "src/pages/ArrivalAnimation.h"
#include "src/pages/IncomingCall.h"
#include "src/pages/WeatherPage.h"
#include "src/pages/PageNavigation.h"
#include "src/map/MapState.h"
#include "src/map/VectorMapReceiver.h"
#include "src/map/RasterDecoder.h"
#include "src/display/RowWorker.h"
#include "src/map/RasterRenderer.h"
#include "src/map/RouteRenderer.h"
#include "src/protocol/BulkReceiver.h"
#include "src/power/BatteryPolling.h"
#include "src/connectivity/Notifications.h"
#include "src/map/MapPrimitives.h"
#include "src/map/MapHud.h"
#include "src/map/CameraOverlay.h"
#include "src/map/NavigationMarkers.h"
#include "src/map/RadarEffects.h"
#include "src/map/MapFrame.h"
#include "src/pages/NavigationPage.h"
#include "src/protocol/CommandReceiver.h"
#include "src/connectivity/DeviceName.h"
#include "src/diagnostics/RenderBenchmarks.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "SdgoodsReturn.h"   // 谷仓多应用模式：长按电源键 / 串口 L 返回官方 launcher

// ============ v7.27 开机音效：谷仓板无 ES8311，I2S 直推 Class-D 功放 ============
#include "driver/i2s_std.h"
#include "driver/gpio.h"

static i2s_chan_handle_t chimeTx = nullptr;
#include "resources/BootSound.h"

// 官方 sdgoods_audio.c：I2S_NUM_0 / 16bit 立体声 / MCLK=GPIO15 / BCLK=48 / WS=38 /
// DOUT=47，PA_EN=GPIO3 高有效。开机旋律是 48kHz 单声道，展开成双声道送 I2S。
//
// v7.27.2 开机音量：100 = 原始幅度，50 = 幅度减半（-6dB，即音量降低 50%）。
// 在送 I2S 前对 PCM 做整数缩放，不改动 resources/BootSound.h（无需重新生成资源）。
// 想再调音量只改这里：0 = 静音，75 = 降低 25%，依此类推。
#define BOOT_CHIME_VOLUME_PCT 50

static void bootChime() {
  const uint32_t RATE = BOOT_SOUND_RATE;
  gpio_config_t paCfg = {
    .pin_bit_mask = 1ULL << PA,
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&paCfg);
  gpio_set_level((gpio_num_t)PA, 1);       // 打开功放
  delay(50);                              // PA_SETTLE_MS

  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  if (i2s_new_channel(&cc, &chimeTx, nullptr) != ESP_OK) {
    Serial.println("[snd] i2s new channel fail"); return;
  }
  i2s_std_config_t sc = {};
  sc.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE);
  sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;   // MCLK = 12.288MHz @48k
  sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                    I2S_SLOT_MODE_STEREO);
  sc.gpio_cfg.mclk = (gpio_num_t)I2S_MCK_IO;
  sc.gpio_cfg.bclk = (gpio_num_t)I2S_BCK_IO;
  sc.gpio_cfg.ws   = (gpio_num_t)I2S_WS_IO;
  sc.gpio_cfg.dout = (gpio_num_t)I2S_DO_IO;
  sc.gpio_cfg.din  = I2S_GPIO_UNUSED;
  if (i2s_channel_init_std_mode(chimeTx, &sc) != ESP_OK ||
      i2s_channel_enable(chimeTx) != ESP_OK) {
    Serial.println("[snd] i2s init/enable fail"); return;
  }

  static int16_t stereo[2048];
  const int16_t *pcm = BOOT_SOUND;
  const int32_t vol = BOOT_CHIME_VOLUME_PCT;
  int remain = BOOT_SOUND_LEN;
  while (remain > 0) {
    int n = remain > 1024 ? 1024 : remain;
    for (int i = 0; i < n; i++) {
      int16_t v = (int16_t)((int32_t)pcm[i] * vol / 100);   // 幅度按百分比缩放
      stereo[2 * i] = v; stereo[2 * i + 1] = v;
    }
    size_t done = 0;
    if (i2s_channel_write(chimeTx, stereo, n * 2 * sizeof(int16_t),
                          &done, portMAX_DELAY) != ESP_OK) break;
    pcm += n; remain -= n;
  }
  // 尾部送静音冲掉 DMA 里的残样本，再关功放，避免断电 pop 声
  memset(stereo, 0, sizeof(stereo));
  size_t done = 0;
  i2s_channel_write(chimeTx, stereo, sizeof(stereo), &done, pdMS_TO_TICKS(200));
  delay(60);
  gpio_set_level((gpio_num_t)PA, 0);
  i2s_channel_disable(chimeTx);
  i2s_del_channel(chimeTx);
  chimeTx = nullptr;
  Serial.printf("[snd] boot chime played (I2S direct -> PA, vol=%d%%)\n",
                BOOT_CHIME_VOLUME_PCT);
}

// 开机音效任务：在独立 RTOS 任务里播放音频，与开机动画并行，避免阻塞吞掉 LOGO 动画。
static TaskHandle_t s_chimeTask = nullptr;
static void bootChimeTask(void *arg) {
  (void)arg;
  bootChime();
  s_chimeTask = nullptr;
  vTaskDelete(nullptr);
}

// 初始化：硬件、画布、持久化设置及蓝牙服务。
void setup() {
  boardPowerInit();                                    // 谷仓：先锁住电池自锁闩 GPIO7，防掉电
  makeBleName();                                       // v7.0 先算好蓝牙名（首页要显示）
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);   // 没打开串口监视器时打印不等待（v1.2 的修复）
  Serial.printf("Reset reason=%d, sleep wake=%d\n", (int)esp_reset_reason(),
                (int)esp_sleep_get_wakeup_cause());
  // 143C：从 BOOT(GPIO0) 深睡唤醒时，必须长按 2 秒才真正开机；短按保持关机
#ifdef BOARD_143C
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    pinMode(GPIO_NUM_0, INPUT_PULLUP);
    uint32_t bootHeld = 0, t0 = millis();
    while (millis() - t0 < 2500) {
      if (digitalRead(GPIO_NUM_0) == HIGH) { bootHeld = millis() - t0; break; }
      delay(20);
    }
    bool bootLong = (bootHeld >= 2000) ||
                    (millis() - t0 >= 2500 && digitalRead(GPIO_NUM_0) == LOW);
    if (!bootLong) {
      esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
      rtc_gpio_init(GPIO_NUM_0);
      rtc_gpio_set_direction(GPIO_NUM_0, RTC_GPIO_MODE_INPUT_ONLY);
      rtc_gpio_pullup_en(GPIO_NUM_0);
      rtc_gpio_pulldown_dis(GPIO_NUM_0);
      esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
      esp_deep_sleep_start();
    }
  }
  pinMode(GPIO_NUM_0, INPUT_PULLUP);   // 运行时读取 BOOT 按键
#endif
#ifdef BOARD_SDGOODS
  // 关机（深睡）态：短按只会把芯片唤醒，必须继续按住满 3 秒才真正开机；
  // 未满 3 秒松手就释放电池自锁闩并重新深睡，避免误触开机。
  // 注意：只有 EXT0 深睡唤醒才做这个判定，上电复位 / 烧录后重启不要求长按。
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("[pwr] deep-sleep wake: checking 3s power-key hold");
    uint32_t held = 0, t0 = millis();
    const uint32_t gateMs = SDG_KEY_HOLD_MS + 150;
    while (millis() - t0 < gateMs) {
      if (digitalRead(BOARD_POWER_KEY_PIN) == HIGH) { held = millis() - t0; break; }
      delay(20);
    }
    const bool longPress = (held >= SDG_KEY_HOLD_MS) ||
        (millis() - t0 >= gateMs && digitalRead(BOARD_POWER_KEY_PIN) == LOW);
    if (!longPress) {
      Serial.println("[pwr] short press while off -> stay off");
      gpio_set_level((gpio_num_t)BOARD_BAT_LATCH_PIN, 0);   // 松开自锁闩
      uint32_t wait = millis();
      while (millis() - wait < 8000 &&
             digitalRead(BOARD_POWER_KEY_PIN) == LOW) delay(20);
      esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
      esp_sleep_enable_ext0_wakeup((gpio_num_t)BOARD_POWER_KEY_PIN, 0);
      esp_deep_sleep_start();
    }
    Serial.println("[pwr] 3s hold confirmed -> booting");
  }
#endif
  Wire.begin(IIC_SDA, IIC_SCL);
  touchInit();                                         // v5.2 触摸屏
  batInit();                                           // v6.8 电源芯片（电量）

  // v2.3 读取保存的屏幕数据线速度（默认 40MHz，和官方示例一样）
  {
    Preferences pr;
    pr.begin("motonav", true);
    qspiMHz = pr.getInt("qspi", 40);
    screenRot = pr.getInt("rot", 0) & 3;                            // v6.2 圆屏方向
    setScreenDeg(pr.getInt("rotd", screenRot * 90), false);         // v6.4 任意角度
    bootBrand = BootAnimation::valid(pr.getInt("bootbrand", 0));
    hidMusicMode = pr.getBool("mediahid", false);
    fastFlushOn = pr.getBool("fastf", true);
    pr.end();
    if (qspiMHz != 40 && qspiMHz != 60 && qspiMHz != 80) qspiMHz = 40;
  }
  boardBrightnessInit();                               // 谷仓：GPIO13 LEDC 背光
  if (!gfx->begin(qspiMHz * 1000000)) {
    Serial.println("gfx->begin() failed; stopping before framebuffer use");
    while (true) delay(1000);
  }
  boardBrightness(200);

  iconBuf = (uint16_t *)ps_malloc(ICON_BUF_W * ICON_BUF_H * 2);   // v1.8.1 图标缓存
  buildRingLut();                                                   // v1.8.3 抗锯齿圆环查找表
  mapInit();                                                        // v2.0 小地图缓冲
  for (int i = 0; i < IMG_SLOTS; i++) {
    imgs[i].show = (uint8_t *)ps_malloc(IMG_MAX);
    imgs[i].recv = (uint8_t *)ps_malloc(IMG_MAX);
    imgs[i].showW = 0; imgs[i].showH = 0; imgs[i].recvId = -1;
  }

  // Begin with black instead of briefly displaying the waiting page before the intro.
  gfx->fillScreen(0);
  gfx->flush();
  initRotSpan();                                                    // v6.4 任意角度用
  initDoubleBuffer();                                               // v2.2 双缓冲 + 核心 0 刷屏
  helperInit();                                                     // v5.7 另一个核心帮忙画底图

  if (bootBrand != BootAnimation::Off && gfx->getFramebuffer()) {
    uint32_t bootAt = millis();
    const uint32_t animFull = 4300;   // 完整 LOGO 定格时刻（淡出起点之前）
    const uint32_t holdMs  = 1000;    // v7.27.2 动画播完后，最后一帧停顿 1 秒再切待机
    BootAnimation::render(gfx->getFramebuffer(), 0, bootBrand);      // 先出第一帧 LOGO
    flushSync();
    // 开机动画与开机音效同时开始：音效放到独立任务播放，动画在主循环并行渲染（不再被阻塞的音效吞掉）
    if (s_chimeTask == nullptr)
      xTaskCreate(bootChimeTask, "bootChime", 8192, nullptr, 1, &s_chimeTask);
    // 旋律播完 → 把入场动画补完到“完整显示”那一刻（intro 已过的稳态 LOGO，无淡出）
    while (millis() - bootAt < animFull) {
      uint32_t frameAt = millis();
      BootAnimation::render(gfx->getFramebuffer(), frameAt - bootAt, bootBrand);
      flushSync();
      uint32_t spent = millis() - frameAt;
      delay(spent < 33 ? 33 - spent : 1);
    }
    // 定格在最后一帧（完整 LOGO，gain=1，无淡出），停顿 holdMs 再切到待机画面
    BootAnimation::render(gfx->getFramebuffer(), animFull, bootBrand);
    flushSync();
    Serial.printf("[boot] logo hold %ums\n", (unsigned)holdMs);
    delay(holdMs);
  }
  drawWaiting(WAIT_NOLINK);
  flushSync();
  needFlush = false;

#if FPS_SELFTEST
  runFpsSelfTest();
#endif
  BLEDevice::init(bleName);                    // v7.0 名字带地址后 4 位；名字放不进广播包时库会自动放到扫描应答里
  BLEDevice::setMTU(517);     // 允许手机协商大数据包，用来传文字图片
#if defined(CONFIG_NIMBLE_ENABLED)
  // v4.5 允许蓝牙 2M 模式（速度翻倍，手机请求时直接同意）和最长空中数据包（251 字节）
  ble_gap_set_prefered_default_le_phy(BLE_GAP_LE_PHY_1M_MASK | BLE_GAP_LE_PHY_2M_MASK,
                                      BLE_GAP_LE_PHY_1M_MASK | BLE_GAP_LE_PHY_2M_MASK);
  ble_gap_write_sugg_def_data_len(251, 2120);
#endif
  // v6.5 配对验证：圆屏显示 6 位随机配对码（每次配对都换），手机输入；要绑定 + 防中间人(MITM) + 安全连接
  {
    BLESecurity *sec = new BLESecurity();                 // 构造时设好“交换加密密钥和身份密钥”（手机换地址也认得出）
    // v7.23 关键修复：配对码不显示 + 约60秒自动断开 的根因都是 MITM=false。
    // 当 MITM=false 时，BLE SMP 会降级为 Just-Works（无需用户交互）→ onPassKeyNotify 不触发 → 圆屏不显码；
    // 且 iOS 的 HID 服务【要求认证绑定】，拿到未认证的 Just-Works 绑定后会在约60秒主动断开。
    // 因此必须 MITM=true：SMP 才会选择 Passkey Display（圆屏显码、手机输入），建立“认证绑定”，iOS 才保持连接。
    // 同项目 175/185b 屏型均使用 setAuthenticationMode(true,true,true) 且工作正常，143 这版此前被误改成 false。
    sec->setCapability(ESP_IO_CAP_OUT);                   // 圆屏显示配对码、手机输入
    sec->setAuthenticationMode(true, true, true);         // 绑定 + 防中间人(MITM) + 安全连接：MITM=true 才会弹配对码且 iOS HID 不断开
    sec->setPassKey(false);                               // 随机配对码
    sec->regenPassKeyOnConnect(true);                     // 每次配对重新随机
    BLESecurity::setForceAuthentication(true);            // 连接即发起安全请求，iOS 才会弹配对码
    BLEDevice::setSecurityCallbacks(new SecCB());
  }
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCB());
  BLEService *svc = server->createService(SERVICE_UUID);
  // v7.19 命令通道（Nordic UART 风格）不再要求加密/认证才能写。
  // 原因：iOS 客户端在链路完成加密认证前就发命令，带 WRITE_AUTHEN 的特征值会被 GATT 直接拒绝，
  // 导致 onWrite 不触发、app 命令到不了设备（安卓写被拒会自动触发配对重试，iOS 不会）。
  // 配对/绑定仍用于 HID 媒体与 TX 通知；命令通道保持始终可写，确保 iOS 也能控制。
  BLECharacteristic *rx = svc->createCharacteristic(
    RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  rx->setCallbacks(new RxCB());
  txChar = svc->createCharacteristic(TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);   // v2.0
  txChar->addDescriptor(new BLE2902());
  txChar->setCallbacks(new TxCB());                 // v7.24 诊断：记录 App 是否订阅 TX 通知
  svc->start();
#ifndef MOTONAV_NO_HID
  initMediaHid(server);
#endif
  appleMedia.begin();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
#ifndef MOTONAV_NO_HID
  adv->addServiceUUID(BLEUUID((uint16_t)0x1812));
  adv->setAppearance(GENERIC_HID);
#endif
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  const MotoNavIpcStackStatus ipc=motoNavIpcStackStatus();
  Serial.printf("IPC stacks bytes=%lu/%lu free-min=%lu/%lu\n",
      (unsigned long)ipc.bytes[0],(unsigned long)ipc.bytes[1],
      (unsigned long)ipc.freeMinimum[0],(unsigned long)ipc.freeMinimum[1]);
  idlePowerOffReset(&idlePowerOffTimer, millis());
  Serial.println(String("MotoNav ready, firmware ") + FW_VERSION);
}

#include "src/power/Shutdown.h"
#include "src/graphics/JunctionOverlay.h"

#ifdef BOARD_SDGOODS
// ============ v7.28 唯一电源键 GPIO6（低有效）的三套语义 ============
//   1) 长按 3s：开机态 -> 关机；关机态 -> 开机（setup 里的深睡唤醒判定）
//   2) 待机界面单击：切换「待机动画」与「待机屏保」(homeBgMode 1 / 0)
//   3) 导航 / 模拟导航 / 漫游(巡航) 单击：循环切换 12 套地图风格 themeIdx
static void sdgoodsPowerKeyTap() {
  const bool standby = onWaitScreen && page == 0 && !inNav && !sliding &&
                       !callOn && !fxOn && !pairOn;
  const bool navigating = inNav || cruiseOn;   // 模拟导航走 inNav；漫游 = cruiseOn
  if (standby) {
    homeBgMode = homeBgMode ? 0 : 1;
    standbyIdleSince = millis();              // 手动切换也算一次操作，重置 1 分钟计时
    Serial.printf("[key] tap -> standby screen %d\n", homeBgMode);
    drawWaiting(waitSt);
  } else if (navigating) {
    themeIdx = (themeIdx + 1) % THEME_N;
    navDirty = true;
    Serial.printf("[key] tap -> map style %d/%d (inNav=%d cruise=%d)\n",
                  themeIdx, THEME_N, (int)inNav, (int)cruiseOn);
  } else {
    Serial.printf("[key] tap -> ignored (onWait=%d page=%d inNav=%d cruise=%d)\n",
                  (int)onWaitScreen, page, (int)inNav, (int)cruiseOn);
  }
}

static void sdgoodsPowerKeyPoll() {
  static bool prev = false, longFired = false;
  static uint32_t downAt = 0;
  const uint32_t now = millis();
  const bool down = digitalRead(BOARD_POWER_KEY_PIN) == LOW;
  if (down && !prev) { downAt = now; longFired = false; }
  // 长按不必等松手，满 3s 立即关机（devicePowerOff 内部会等松手再深睡）
  if (down && !longFired && now - downAt >= SDG_KEY_HOLD_MS) {
    longFired = true;
    Serial.println("[key] long press 3s -> power off");
    devicePowerOff();                          // 不会返回
  }
  if (!down && prev) {
    const uint32_t dur = now - downAt;
    if (!longFired && dur >= SDG_KEY_TAP_MIN_MS && dur < SDG_KEY_HOLD_MS)
      sdgoodsPowerKeyTap();
  }
  prev = down;
}
#endif

// 主循环：接收消息、处理输入、调度页面、绘图和推屏。
void loop() {
  sdgoodsReturnPoll();                                 // 谷仓：返回启动器入口（多应用模式硬性要求）
  // 串口自检：发 't' 打印触摸坐标映射与音乐按钮命中结果（无需手指，回归用）
  if (Serial.available() && Serial.peek() == 't') {
    Serial.read();
    touchSelfTest();
  }
#ifdef BOARD_SDGOODS
  sdgoodsPowerKeyPoll();   // 单键统一处理：长按开关机 / 待机切换 / 导航切地图
  {

    // 待机画面自动切换：进待机 / 退出导航默认回 mode1；满 1 分钟无操作切 mode0。
    static bool prevOnWait = false;
    static bool wasInNav = false;
    if (onWaitScreen && !prevOnWait) { homeBgMode = 1; standbyIdleSince = millis(); drawWaiting(waitSt); }
    if (wasInNav && !inNav)          { homeBgMode = 1; standbyIdleSince = millis(); drawWaiting(waitSt); }
    if (!wasInNav && inNav)          { standbyIdleSince = 0; }
    prevOnWait = onWaitScreen;
    wasInNav = inNav;
    if (onWaitScreen && page == 0 && !inNav && !sliding && !callOn && !fxOn && !pairOn
        && homeBgMode == 1 && standbyIdleSince && (millis() - standbyIdleSince >= 60000)) {
      homeBgMode = 0;
      standbyIdleSince = 0;
      drawWaiting(waitSt);
      Serial.println("[standby] idle 60s -> Fuji/711 screen (mode0)");
    }
  }
#endif
  homeClock.tick(millis());
  hidMediaTick();                                      // press/release without blocking navigation
  static uint32_t lyricConnectionEpoch = 0;
  if (lyricConnectionEpoch != mediaConnectionEpoch) {
    lyricConnectionEpoch = mediaConnectionEpoch; musicLyrics = false; lyricImageGeneration = 0;
    imgs[12].showW = 0; imgs[12].showH = 0; imgs[12].recvId = -1; musicDirty = true;
  }
  appleMedia.tick(bleConnected && bleAuthed && hidMusicMode, bleConnHandle, mediaConnectionEpoch, millis());
  static int lastControlMask = -1;
  int controlMask = appleMedia.controlMask();
  if (lastControlMask != controlMask) { lastControlMask = controlMask; pendingMediaStatus = String("AMC,") + controlMask; musicDirty = true; Serial.printf("MEDIA AMS controls=%d\n", controlMask); }
  if (appleMedia.controlAck) { musicAck = appleMedia.controlAck > 0 ? 9 : 10; appleMedia.controlAck = 0; musicAckUntil = millis() + 2500; musicDirty = true; }
  if (appleMedia.changed) {
    appleMedia.changed = false; musicDirty = true;
    static uint32_t lyricGeneration = 0; static uint16_t lyricTitleRevision = 0, lyricArtistRevision = 0;
    if (lyricGeneration != appleMedia.generation || lyricTitleRevision != appleMedia.values[0].revision || lyricArtistRevision != appleMedia.values[1].revision) {
      lyricImageGeneration = 0;
      lyricGeneration = appleMedia.generation; lyricTitleRevision = appleMedia.values[0].revision; lyricArtistRevision = appleMedia.values[1].revision;
      imgs[12].showW = 0; imgs[12].showH = 0; imgs[12].recvId = -1;
    }
    if (hidMusicMode) { musicHave = appleMedia.available && appleMedia.values[0].text.length(); musicPlaying = appleMedia.playback.known && appleMedia.playback.state == 1; }
  }
  static unsigned long mediaDrawAt = 0;
  if (hidMusicMode && appleMedia.available && millis() - mediaDrawAt >= 1000) { mediaDrawAt = millis(); musicDirty = true; }
  saveRotIfDue();                                      // v6.4
  // v6.5 连上 60 秒还没通过验证（不是自己的手机）：断开，让自己的手机能连
#if defined(CONFIG_NIMBLE_ENABLED)
  // v7.19 仅当本机曾绑定过其他设备、且当前连接迟迟未加密(未配对)时才超时踢断；避免纯 RX 用法或首次配对被反复踢断
  if (bleConnected && bleConnHandle != 0xFFFF && millis() - bleConnAt > AUTH_TIMEOUT_MS && (bondCount() > 0 && !bleAuthed)) {
    Serial.printf("[auth] 60s 超时踢断：bond=%d authed=%d（正常配对后 authed 应为 1，此分支不应触发）\n", bondCount(), (int)bleAuthed);
    ble_gap_terminate(bleConnHandle, BLE_ERR_REM_USER_CONN_TERM);
    bleConnAt = millis();                              // 别每圈都断
  }
#endif
  // 1. 连接状态变化（先处理）；v6.5 通过验证才算连上
  bool conn = bleConnected;                  // v7.19 已连接即视为“连上”，不再要求加密验证（iOS Just-Works 不认证）
  if (conn != shownConn) {
    shownConn = conn;
    if (conn) {
      lastRxMs = millis();
      drawWaiting(WAIT_READY);
    } else {
      trafficLightFrame.clear();
      trafficLightLab.clear();
      bool wasNav = inNav;
      resetNavState();
      staleShown = false;
      drawWaiting(wasNav ? WAIT_LOST : WAIT_NOLINK);   // 导航途中断开 → 红色 LINK LOST
    }
  }

  // 2. 取出队列里的所有消息逐条处理（最多 30 条，避免卡住画面）
  static RxMsg local;
  for (int i = 0; i < 30; i++) {
    bool got = false;
    portENTER_CRITICAL(&mux);
    if (rxTail != rxHead) {
      local.len = rxq[rxTail].len;
      memcpy(local.data, rxq[rxTail].data, local.len);
      rxTail = (rxTail + 1) % RXQ_SLOTS;
      got = true;
    }
    portEXIT_CRITICAL(&mux);
    if (!got) break;
    handleMessage(local.data, local.len);
  }

  updateStats();
  if (pendingStat.length()) { pendingStat2 = pendingStat; pendingStat = ""; }
  sendPendingAck();                                    // v2.0 地图收齐的确认 / v2.2 帧率

  // 3. 进度环动画；小地图模式下连续刷新（v2.2 不再限制帧间隔）
  animateRing();
  // 地图帧原本连续刷新；大箭头画面也按现有帧节奏推进行程环。
  static unsigned long tripDrawAt=0;
  static float tripDrawValue=-1;
  float tripValue=tripFrac();
  if(inNav && progOn && tripOn && !cruiseOn && !ringActive && !staleShown &&
     tripValue>=0 && tripValue!=tripDrawValue && millis()-tripDrawAt>=33) {
    tripDrawAt=millis();tripDrawValue=tripValue;navDirty=true;
  }
  if(tripValue<0)tripDrawValue=-1;
  static bool wasMap = false;
  bool mapNow = inNav && mapModeActive();
  if (mapNow != wasMap) { wasMap = mapNow; navDirty = true; }   // 进出地图模式时整帧重画
  if (mapNow) navDirty = true;

  touchPoll();                                         // v5.2 按住接听 / v5.3 滑动换页
  batPoll();                                           // v6.8 电量
  // 4. 需要时重画导航画面
  // v6.5 配对：整屏显示配对码，配好 / 失败 / 60 秒 / 断开后回到原来的画面
  if (pairReq) {
    pairReq = false;
    if (sliding) dragAbort();
    pairOn = true; pairShowAt = millis();
    drawPairScreen();
  }
  if (pairOn && (pairDone || millis() - pairShowAt > AUTH_TIMEOUT_MS || !bleConnected)) {
    pairOn = false; pairDone = false;
    if (page == 2) musicDirty = true;
    else if (page == 1) wxDirty = true;
    else if (inNav) navDirty = true;
    else drawWaiting(conn ? WAIT_READY : WAIT_NOLINK);
  }
  // v5.1 来电：整屏显示（约 12 帧/秒的动画），结束后回到原来的画面
  static bool wasCall = false;
  if (callOn && (millis() - callSeenAt > 120000 || !conn)) callOn = false;      // 2 分钟没消息 / 断开：收起
  if (callOn && sliding) dragAbort();                  // v5.5 拖到一半来电：放弃换页
  if (callOn) {
    static unsigned long lastCallDraw = 0;
    if (millis() - lastCallDraw > 80) { lastCallDraw = millis(); drawCallScreen(); }
    wasCall = true;
  } else if (wasCall) {
    wasCall = false;
    if (page == 2) musicDirty = true;
    else if (page == 1) wxDirty = true;
    else if (inNav) navDirty = true;
    else drawWaiting(conn ? WAIT_READY : WAIT_NOLINK);
  }

  // v7.1 到达特效：来电、配对优先；放完 / 被点掉后回到原来的画面
  static bool wasFx = false;
  if (fxOn && (millis() - fxAt > fxDur || !conn)) fxOn = false;
  if (fxOn) {
    static unsigned long lastFxDraw = 0;
    if (!callOn && !pairOn && page == 0 && !sliding && millis() - lastFxDraw >= 33) {
      lastFxDraw = millis();
      drawArriveFx(millis() - fxAt);
    }
    wasFx = true;
  } else if (wasFx) {
    wasFx = false;
    if (!callOn && !pairOn) {
      if (page == 2) musicDirty = true;
      else if (page == 1) wxDirty = true;
      else if (inNav) navDirty = true;
      else drawWaiting(conn ? WAIT_READY : WAIT_NOLINK);
    }
  }

  musicTick();
  weatherTick();                                       // v5.3 天气页
  if (inNav && navDirty && !fxOn && !callOn && !pairOn && page == 0 && !sliding) {
    drawNavFrame();
    navDirty = false;
  }

  // 5. 导航中长时间收不到任何消息 → 数据中断提示
  if (inNav && conn && !fxOn && !callOn && !pairOn && page == 0 && !sliding && !staleShown && millis() - lastRxMs > STALE_MS) {
    staleShown = true;
    drawStaleOverlay();
  }

  // Compose over navigation, never replace the screen or alter route/arrival state.
  const uint32_t lightNow=millis();
  if (!conn || !trafficLightLab.valid(lightNow)) trafficLightLab.clear();
  const bool lightPreview=trafficLightPreviewUntil && (int32_t)(trafficLightPreviewUntil-lightNow)>0;
  if(!lightPreview)trafficLightPreviewUntil=0;
  const bool showLight=(trafficLightLab.valid(lightNow) || trafficLightFrame.valid(lightNow) || lightPreview) && conn && page==0 && !sliding && !pairOn && !callOn && !fxOn && !staleShown;
  static bool lightShown=false;
  static uint32_t lightDrawAt=0;
  drawStandbyAnim();
  drawHomeClock();
  if(showLight) {
    if(!lightShown || needFlush || lightNow-lightDrawAt>=200) {
      drawTrafficLightLab(lightNow);lightDrawAt=lightNow;
    }
  } else if(lightShown && !pairOn && !callOn && !fxOn && !sliding) {
    if(page==2)musicDirty=true;
    else if(page==1)wxDirty=true;
    else if(inNav)navDirty=true;
    else drawWaiting(conn ? WAIT_READY : WAIT_NOLINK);
  }
  if(lightShown!=showLight)Serial.printf("TL OVERLAY visible=%d inNav=%d at=%lu valid=%d conn=%d page=%d sliding=%d pair=%d call=%d fx=%d stale=%d\n",
    showLight,inNav,lightNow,trafficLightLab.valid(lightNow),conn,page,sliding,pairOn,callOn,fxOn,staleShown);
  lightShown=showLight;

  // 6. 有变化才把画布推到屏幕（小地图帧异步推送，其他画面同步推送）
  if (needFlush && !sliding) {
    if (frameIsMap) flushAsync(); else flushSync();
    needFlush = false;
    frameIsMap = false;
  }
#ifndef BOARD_143C
  idlePowerPollSupply();
  // Idle phone traffic does not count as navigation; external power inhibits off.
  const bool navigating = idlePowerOffNavigationActive(
      bleConnected, inNav, cruiseOn, navDir == "A");   // v7.19 iOS 不要求加密验证
  const bool interacting = pairOn || pairReq || callOn || touchDown || dragging || sliding;
  if (idlePowerOffDueOnBattery(&idlePowerOffTimer, millis(), navigating, interacting, &idlePowerSupply)) {
    bool queued;
    portENTER_CRITICAL(&mux);
    queued = rxTail != rxHead;
    portEXIT_CRITICAL(&mux);
    if (!queued) {
      // Never decide from the 5-second battery UI cache. Re-read at the deadline.
      idlePowerPollSupply(true);
      if (idlePowerOffDueOnBattery(&idlePowerOffTimer, millis(), navigating, interacting, &idlePowerSupply))
        idlePowerOffShutdown();
    }
  }
#else
  // 143C：取消“无动作自动关机”，设备一直开机。
  // BOOT(GPIO0) 按键：长按 2 秒关机；已连接手机时短按循环切换地图风格。
  {
    static bool bootPrev = false;
    static uint32_t bootDownAt = 0;
    static bool bootLongFired = false;
    bool bootDown = digitalRead(GPIO_NUM_0) == LOW;        // 按下 = 低电平
    if (bootDown && !bootPrev) { bootDownAt = millis(); bootLongFired = false; }
    else if (!bootDown && bootPrev) {
      uint32_t dur = millis() - bootDownAt;
      if (!bootLongFired && dur >= 50 && dur < 2000) {
        // 待机状态下短按：切换两套待机画面（CC110 背景 / 原版骑手动画）
        if (onWaitScreen && page == 0 && !inNav && !sliding) {
          homeBgMode = homeBgMode ? 0 : 1;
          standbyIdleSince = millis();   // 手动切换也算一次操作，重置 1 分钟计时
          Serial.printf("BOOT short press -> standby screen %d\n", homeBgMode);
          drawWaiting(waitSt);
        } else if (bleConnected) {
          // 导航/其它界面：短按循环切换地图风格
          themeIdx = (themeIdx + 1) % THEME_N;
          Serial.printf("BOOT short press -> map style %d\n", themeIdx);
          if (inNav) navDirty = true;
        }
      }
    }
    if (bootDown && !bootLongFired && (millis() - bootDownAt) >= 2000) {
      bootLongFired = true;
      Serial.println("BOOT long press 2s -> power off");
      devicePowerOff();
    }
    bootPrev = bootDown;

    // 待机画面自动切换：进入待机 / 导航退出默认回“点赞小人+cc110摩托”(mode1)；
    // 该画面 1 分钟无任何操作则自动切到“富士山711摩托”(mode0)。短按 BOOT 切换功能不变。
    static bool prevOnWait = false;
    static bool wasInNav = false;
    if (onWaitScreen && !prevOnWait) { homeBgMode = 1; standbyIdleSince = millis(); drawWaiting(waitSt); }  // 进入待机默认 mode1 并整屏重绘
    if (wasInNav && !inNav)          { homeBgMode = 1; standbyIdleSince = millis(); drawWaiting(waitSt); }  // 导航退出回 mode1 并立即整屏重绘（消除残留重叠）
    if (!wasInNav && inNav)          { standbyIdleSince = 0; }                              // 进入导航：停止待机计时
    prevOnWait = onWaitScreen;
    wasInNav = inNav;
    // 仅在真正“待机且无覆盖层”时计时触发；导航/来电/特效/配对/滑动/非首页均不触发
    if (onWaitScreen && page == 0 && !inNav && !sliding && !callOn && !fxOn && !pairOn
        && homeBgMode == 1 && standbyIdleSince && (millis() - standbyIdleSince >= 60000)) {
      homeBgMode = 0;                 // 1 分钟无操作 → 自动切到富士山711摩托
      standbyIdleSince = 0;
      drawWaiting(waitSt);
      Serial.println("[standby] idle 60s -> Fuji/711 screen (mode0)");
    }
  }
#endif
  delay(mapNow ? 1 : 10);
}
