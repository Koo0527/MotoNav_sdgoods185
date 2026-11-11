#pragma once
#include <stdint.h>

// 谷仓次元屏物理面板 360x360，MotoNav 逻辑画布 466x466。
// 刷屏时每个物理像素用双线性插值从逻辑画布取样；触摸坐标用同一套中心映射反向换算。
// 来源：boards/185b/MotoNav/PortScale.h（微雪 1.85B 同款 ST77916 360x360 圆屏，已真机验证）。
class PortScale {
public:
  static constexpr int LOGICAL = 466, PHYSICAL = 360;
  int32_t axis[PHYSICAL];
  PortScale() {
    for (int p = 0; p < PHYSICAL; p++)
      axis[p] = (int32_t)(((int64_t)(2 * p + 1) * LOGICAL * 32768) / PHYSICAL - 32768);
  }
  static int touch(int raw) {
    if (raw < 0) raw = 0;
    if (raw >= PHYSICAL) raw = PHYSICAL - 1;
    return ((2 * raw + 1) * LOGICAL) / (2 * PHYSICAL);
  }
  static uint32_t spread(uint16_t c) {
    return ((uint32_t)c | ((uint32_t)c << 16)) & 0x07E0F81Fu;
  }
  // 最近邻取样：把旋转做完后直接取最近的一个源像素，跳过双线性插值的 4 次读。
  // 只在「跟手拖动 / 换页动画」这十几帧里用（见 DisplayPipeline 的 pushNearest）：
  // 画面本来就在高速平移，肉眼分辨不出插值差异，但推屏开销从 4 次 PSRAM 读降到 1 次。
  uint16_t pixelNearest(const uint16_t *source, int x, int y, int rotation,
                        bool freeRotation, int32_t cosine, int32_t sine) const {
    int32_t fx = axis[x], fy = axis[y];
    constexpr int32_t center = (LOGICAL - 1) * 32768;
    if (freeRotation) {
      int32_t dx = fx - center, dy = fy - center;
      fx = (int32_t)(((int64_t)dx * cosine + (int64_t)dy * sine) >> 16) + center;
      fy = (int32_t)((-(int64_t)dx * sine + (int64_t)dy * cosine) >> 16) + center;
    } else if (rotation == 1) { int32_t a = fx; fx = fy; fy = 2 * center - a; }
    else if (rotation == 2)   { fx = 2 * center - fx; fy = 2 * center - fy; }
    else if (rotation == 3)   { int32_t a = fx; fx = 2 * center - fy; fy = a; }
    int sx = (fx + 32768) >> 16, sy = (fy + 32768) >> 16;      // +0.5 四舍五入
    if ((unsigned)sx >= LOGICAL || (unsigned)sy >= LOGICAL) return 0;
    return source[sy * LOGICAL + sx];
  }

  uint16_t pixel(const uint16_t *source, int x, int y, int rotation,
                 bool freeRotation, int32_t cosine, int32_t sine) const {
    int32_t fx = axis[x], fy = axis[y];
    constexpr int32_t center = (LOGICAL - 1) * 32768;
    if (freeRotation) {
      int32_t dx = fx - center, dy = fy - center;
      fx = (int32_t)(((int64_t)dx * cosine + (int64_t)dy * sine) >> 16) + center;
      fy = (int32_t)((-(int64_t)dx * sine + (int64_t)dy * cosine) >> 16) + center;
    } else if (rotation == 1) { int32_t a = fx; fx = fy; fy = 2 * center - a; }
    else if (rotation == 2)   { fx = 2 * center - fx; fy = 2 * center - fy; }
    else if (rotation == 3)   { int32_t a = fx; fx = 2 * center - fy; fy = a; }
    int sx = fx >> 16, sy = fy >> 16;
    if ((unsigned)sx >= LOGICAL - 1 || (unsigned)sy >= LOGICAL - 1) {
      int nx = (fx + 32768) >> 16, ny = (fy + 32768) >> 16;
      return (unsigned)nx < LOGICAL && (unsigned)ny < LOGICAL
               ? source[ny * LOGICAL + nx] : 0;
    }
    const uint16_t *a = source + sy * LOGICAL + sx;
    uint32_t ax = (fx >> 11) & 31, ay = (fy >> 11) & 31;
    uint32_t top    = ((spread(a[0]) * (32 - ax) + spread(a[1]) * ax) >> 5) & 0x07E0F81Fu;
    uint32_t bottom = ((spread(a[LOGICAL]) * (32 - ax) +
                        spread(a[LOGICAL + 1]) * ax) >> 5) & 0x07E0F81Fu;
    uint32_t result = ((top * (32 - ay) + bottom * ay) >> 5) & 0x07E0F81Fu;
    return (uint16_t)(result | (result >> 16));
  }
};
