#pragma once
// 手机文字位图分包接收。由 MotoNav.ino 按依赖顺序包含，仅编译一次。
// 'B' slot id part parts wLo wHi h offLo offHi + 数据
void handleBitmap(const uint8_t *d, int n) {
  if (n < 10) return;
  int slot = d[1], id = d[2], part = d[3], parts = d[4];
  int w = d[5] | (d[6] << 8), h = d[7];
  int off = d[8] | (d[9] << 8);
  if (slot >= IMG_SLOTS) return;
  TextImg &im = imgs[slot];
  if (w == 0 || h == 0 || parts == 0) {          // 空图：清掉这一栏
    im.showW = 0; im.showH = 0; im.recvId = -1;
    navDirty = true;
    if (slot == 3 || slot == 4 || (slot >= 7 && slot <= 11)) wxDirty = true;
    if (slot == 5 || slot == 6 || slot == 12 || slot == 13) musicDirty = true;
    return;
  }
  // v7.28.5 歌词不再要求 HID 模式：AMS（iOS 音乐推送）可用即可收歌词图
  if (slot == 12 && (!musicLyrics || !appleMedia.available ||
      lyricImageGeneration != appleMedia.generation || lyricImageTitleRevision != appleMedia.values[0].revision || lyricImageArtistRevision != appleMedia.values[1].revision)) return;
  int total = ((w + 1) / 2) * h;
  if (total > (slot == 13 ? IMG_ART_MAX : IMG_MAX) || parts > 32) return;   // 封面槽 13 允许到 255×255
  if (id != im.recvId) {                          // 新的一张图：重新开始接收
    im.recvId = id; im.recvW = w; im.recvH = h; im.recvParts = parts; im.recvMask = 0;
  }
  int len = n - 10;
  if (off + len > total) len = total - off;
  if (len > 0) memcpy(im.recv + off, d + 10, len);
  im.recvMask |= (1UL << part);
  uint32_t full = (parts >= 32) ? 0xFFFFFFFFUL : ((1UL << parts) - 1);
  if ((im.recvMask & full) == full) {             // 收齐了：换成显示用的图
    uint8_t *t = im.show; im.show = im.recv; im.recv = t;
    im.showW = im.recvW; im.showH = im.recvH;
    im.recvId = -1;
    navDirty = true;
    if (slot == 3 || slot == 4 || (slot >= 7 && slot <= 11)) wxDirty = true;
    if (slot == 5 || slot == 6 || slot == 12 || slot == 13) musicDirty = true;                // 音乐文字图片收齐后刷新
  }
}

// ===== v7.28.6 彩色专辑封面（RGB565）=====
// 'C' 编号 片序 总片数 wLo wHi h off(4字节LE) + RGB565 小端数据
// 总片数 ≤255，同一片允许拆多次 BLE 写（按 off 累加）；收齐即换显示。
// w/h 为 0 表示清除封面。封面属设备展示数据，与歌词/文字图互不影响。
void handleColorArt(const uint8_t *d, int n) {
  if (n < 11) return;
  const int id = d[1], part = d[2], parts = d[3];
  const int w = d[4] | (d[5] << 8), h = d[6];
  const uint32_t off = (uint32_t)d[7] | ((uint32_t)d[8] << 8) | ((uint32_t)d[9] << 16) | ((uint32_t)d[10] << 24);
  if (w == 0 || h == 0) {                       // 空图：清除封面
    artColor.w = artColor.h = 0; artColor.id = -1;
    musicDirty = true;
    return;
  }
  if (w > 255 || h > 255 || parts == 0 || parts > 255) return;
  const uint32_t total = (uint32_t)w * h * 2;
  if (id != artColor.id || parts != artColor.recvParts || w != artColor.recvW || h != artColor.recvH) {
    artColor.id = id; artColor.recvW = w; artColor.recvH = h; artColor.recvParts = parts;
    memset(artColor.recvMask, 0, sizeof(artColor.recvMask));
  }
  uint32_t len = n - 11;
  if (off >= total) return;
  if (off + len > total) len = total - off;
  if (len > 0 && artColor.recv) memcpy((uint8_t *)artColor.recv + off, d + 11, len);
  if (part < 255) artColor.recvMask[part >> 3] |= 1 << (part & 7);
  bool full = true;
  for (int p = 0; p < parts; p++)
    if (!(artColor.recvMask[p >> 3] & (1 << (p & 7)))) { full = false; break; }
  if (full) {
    uint16_t *t = artColor.show; artColor.show = artColor.recv; artColor.recv = t;
    artColor.w = artColor.recvW; artColor.h = artColor.recvH;
    artColor.id = -1;
    musicDirty = true;
    Serial.printf("MEDIA ART color %ux%u received\n", artColor.w, artColor.h);
  }
}
