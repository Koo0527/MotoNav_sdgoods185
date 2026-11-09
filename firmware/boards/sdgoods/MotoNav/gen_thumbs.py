# -*- coding: utf-8 -*-
# 把 WorkBuddy 资料库里的 thumbs_up.gif（160x256, 7 帧, 带透明）转成固件待机动画资源：
# ThumbsUp.h —— THUMB_RGB565[帧][W*H]（透明像素置 0）+ THUMB_MASK[帧][(W*H+7)/8]（1bit 透明掩码）
# 固件在模式 1（原骑手动画位）直接循环播放这 7 帧，黑底/文字层不变。
import os
from PIL import Image

BASE = r"C:\Users\William Koo\WorkBuddy\2026-10-24-23-23-18\MotoNav\firmware\boards\143\MotoNav"
OUT = os.path.join(BASE, "resources", "ThumbsUp.h")

THUMB_W = 100
THUMB_H = 160
FPW = (THUMB_W * THUMB_H + 7) // 8

im = Image.open(os.path.join(BASE, "thumbs_up.gif"))
n = getattr(im, "n_frames", 1)
print("frames:", n, "src size:", im.size)

rgb_frames = []
mask_frames = []
for i in range(n):
    im.seek(i)
    rgba = im.convert("RGBA").resize((THUMB_W, THUMB_H), Image.LANCZOS)
    px = rgba.load()
    rgb = []
    mask = bytearray(FPW)
    for y in range(THUMB_H):
        for x in range(THUMB_W):
            r, g, b, a = px[x, y]
            idx = y * THUMB_W + x
            if a < 128:
                rgb.append(0)
            else:
                rgb.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
                mask[idx >> 3] |= (1 << (idx & 7))
    rgb_frames.append(rgb)
    mask_frames.append(bytes(mask))

with open(OUT, "w", encoding="utf-8") as f:
    f.write("#pragma once\n")
    f.write("// 待机画面模式1 动画人物：thumbs_up.gif（来自 WorkBuddy 资料库）7 帧循环。\n")
    f.write("// 由 gen_thumbs.py 自动生成；HomePage.h drawThumbFrame() 直接写帧缓冲并跳过透明像素。\n")
    f.write("#include <stdint.h>\n")
    f.write("#define THUMB_W %d\n" % THUMB_W)
    f.write("#define THUMB_H %d\n" % THUMB_H)
    f.write("#define THUMB_FRAMES %d\n" % n)
    f.write("#define THUMB_FPW %d\n" % FPW)
    # RGB565
    f.write("const uint16_t THUMB_RGB[THUMB_FRAMES * THUMB_W * THUMB_H] = {\n")
    flat = [v for fr in rgb_frames for v in fr]
    for i in range(0, len(flat), 12):
        f.write(",".join("0x%04X" % v for v in flat[i:i + 12]) + ",\n")
    f.write("};\n")
    # 透明掩码
    f.write("const uint8_t THUMB_MASK[THUMB_FRAMES * THUMB_FPW] = {\n")
    mflat = [v for fr in mask_frames for v in fr]
    for i in range(0, len(mflat), 24):
        f.write(",".join("0x%02X" % v for v in mflat[i:i + 24]) + ",\n")
    f.write("};\n")
print("written:", OUT, "rgb bytes", len(flat) * 2, "mask bytes", len(mflat))
