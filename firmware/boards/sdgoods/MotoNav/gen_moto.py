# -*- coding: utf-8 -*-
# 把 WorkBuddy 资料库里的 motorcycle_running.gif（844x543, 10 帧, 白底不透明）转成固件待机动画资源：
# Motorcycle.h —— MOTO_RGB565[帧][W*H]（白底像素置 0）+ MOTO_MASK[帧][(W*H+7)/8]（1bit 透明掩码）
# 白底判定：R/G/B 均 >= 248 视为透明（含 254/255 抗锯齿白边）。
# 固件在模式 1（原骑手动画位）循环播放这 10 帧（90ms/帧，一直循环），与 thumbs_up 人物叠加。
import os
from PIL import Image

BASE = r"C:\Users\William Koo\WorkBuddy\2026-10-24-23-23-18\MotoNav\firmware\boards\143\MotoNav"
OUT = os.path.join(BASE, "resources", "Motorcycle.h")

MOTO_W = 180
MOTO_H = 116
FPW = (MOTO_W * MOTO_H + 7) // 8
THRESH = 248  # 白底阈值（>=248 视为透明）

im = Image.open(os.path.join(BASE, "motorcycle_running.gif"))
n = getattr(im, "n_frames", 1)
print("frames:", n, "src size:", im.size, "->", MOTO_W, "x", MOTO_H)

rgb_frames = []
mask_frames = []
for i in range(n):
    im.seek(i)
    rgba = im.convert("RGBA").resize((MOTO_W, MOTO_H), Image.LANCZOS)
    px = rgba.load()
    rgb = []
    mask = bytearray(FPW)
    for y in range(MOTO_H):
        for x in range(MOTO_W):
            r, g, b, a = px[x, y]
            idx = y * MOTO_W + x
            if a < 128 or (r >= THRESH and g >= THRESH and b >= THRESH):
                rgb.append(0)
            else:
                rgb.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
                mask[idx >> 3] |= (1 << (idx & 7))
    rgb_frames.append(rgb)
    mask_frames.append(bytes(mask))

with open(OUT, "w", encoding="utf-8") as f:
    f.write("#pragma once\n")
    f.write("// 待机画面模式1 动画机车：motorcycle_running.gif（来自 WorkBuddy 资料库）10 帧循环。\n")
    f.write("// 由 gen_moto.py 自动生成；白底(RGB>=248)视为透明。HomePage.h drawMotoFrame() 直接写帧缓冲。\n")
    f.write("#include <stdint.h>\n")
    f.write("#define MOTO_W %d\n" % MOTO_W)
    f.write("#define MOTO_H %d\n" % MOTO_H)
    f.write("#define MOTO_FRAMES %d\n" % n)
    f.write("#define MOTO_FPW %d\n" % FPW)
    f.write("const uint16_t MOTO_RGB[MOTO_FRAMES * MOTO_W * MOTO_H] = {\n")
    flat = [v for fr in rgb_frames for v in fr]
    for i in range(0, len(flat), 12):
        f.write(",".join("0x%04X" % v for v in flat[i:i + 12]) + ",\n")
    f.write("};\n")
    f.write("const uint8_t MOTO_MASK[MOTO_FRAMES * MOTO_FPW] = {\n")
    mflat = [v for fr in mask_frames for v in fr]
    for i in range(0, len(mflat), 24):
        f.write(",".join("0x%02X" % v for v in mflat[i:i + 24]) + ",\n")
    f.write("};\n")
print("written:", OUT, "rgb bytes", len(flat) * 2, "mask bytes", len(mflat))
