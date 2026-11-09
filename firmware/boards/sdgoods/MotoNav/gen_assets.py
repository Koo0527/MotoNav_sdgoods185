# -*- coding: utf-8 -*-
# 资源生成脚本：
# 1. JR东日本_ATOS.wav -> resources/BootSound.h (48kHz 单声道 16bit, 峰值归一)
# 2. cc110像素风.jpg   -> 镜像扩张图 cc110_expanded.png -> resources/Cc110Background.h (466x466 RGB565)
#    （扩张目的：圆屏可视区是内切圆，原构图摩托车轮贴画面边缘会被裁掉，需把场景缩小到 0.75 倍）
# 3. 生成最终屏幕效果预览 preview_cc110.png（叠加时钟/ready/版本等文字层）
# 传 preview 参数则只重新生成预览图（跳过音效转换）
import wave, struct, math, sys, os

BASE = r"C:\Users\William Koo\WorkBuddy\2026-10-24-23-23-18\MotoNav\firmware\boards\143\MotoNav"
RES = os.path.join(BASE, "resources")
WAV = r"C:\Users\William Koo\Desktop\JR东日本_ATOS.wav"
IMG = r"C:\Users\William Koo\.workbuddy\clipboard-images\clipboard-2026-10-30T15-26-27-056Z-f793977d.jpg"
IMG_EXPANDED = r"C:\Users\William Koo\WorkBuddy\2026-10-24-23-23-18\MotoNav\cc110_expanded.png"
PREVIEW = r"C:\Users\William Koo\WorkBuddy\2026-10-24-23-23-18\MotoNav\preview_cc110.png"
PREVIEW_ONLY = 'preview' in sys.argv

# ---------------- 1. 音频 ----------------
if not PREVIEW_ONLY:
    w = wave.open(WAV, 'rb')
    assert w.getframerate() == 48000 and w.getsampwidth() == 2
    n = w.getnframes()
    raw = w.readframes(n)
    w.close()
    samples = struct.unpack('<%dh' % (n * 2), raw)
    mono = []
    for i in range(n):
        l, r = samples[2 * i], samples[2 * i + 1]
        v = (l + r) // 2
        mono.append(v)
    peak = max(abs(min(mono)), abs(max(mono)))
    scale = 20000.0 / peak  # 峰值归一到 ~61%FS，避免削波；整体音量由 ES8311 音量寄存器控制
    fade = int(0.008 * 48000)  # 8ms 淡入淡出防爆音
    out = []
    for i, v in enumerate(mono):
        v = int(v * scale)
        if i < fade:
            v = v * i // fade
        elif i > n - fade:
            v = v * (n - i) // fade
        out.append(v)

    with open(os.path.join(RES, 'BootSound.h'), 'w', encoding='utf-8') as f:
        f.write('#pragma once\n')
        f.write('// 开机音效：JR东日本 ATOS 发车旋律（用户提供 WAV 转 48kHz 单声道 16bit，峰值归一+8ms 淡入淡出）\n')
        f.write('// 由 gen_assets.py 自动生成；由 MotoNav.ino 的 bootChime() 经 ES8311/I2S 播放\n')
        f.write('#define BOOT_SOUND_RATE 48000\n')
        f.write('#define BOOT_SOUND_LEN %d\n' % n)
        f.write('#include <stdint.h>\n')
        f.write('const int16_t BOOT_SOUND[BOOT_SOUND_LEN] = {\n')
        for i in range(0, n, 16):
            chunk = out[i:i + 16]
            f.write(','.join(str(v) for v in chunk) + ',\n')
        f.write('};\n')
    print('BootSound.h OK, samples=%d, bytes=%dKB' % (n, n * 2 // 1024))
else:
    print('preview-only: skip BootSound.h')

# ---------------- 2. 背景图 ----------------
from PIL import Image, ImageDraw, ImageFont
# 新版构图（7-Eleven 便利店像素风，1080x1080 方图）：摩托车居中偏下，
# 轮子最低点 y~915/1080（466 系 ~395），距圆心约 162px < 233px 内切圆半径——1:1 映射即可。
img = Image.open(IMG).convert('RGB').resize((466, 466), Image.LANCZOS)
px = img.load()
bg565 = []
for y in range(466):
    for x in range(466):
        r, g, b = px[x, y]
        bg565.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
with open(os.path.join(RES, 'Cc110Background.h'), 'w', encoding='utf-8') as f:
    f.write('#pragma once\n')
    f.write('#include <stdint.h>\n')
    f.write('// 骑行模式主画面背景：7-Eleven 便利店像素风（方图 1:1 缩放，466x466 RGB565）\n')
    f.write('// 由 gen_assets.py 自动生成；HomePage.h drawWaiting() 整屏贴图，文字层叠加\n')
    f.write('#define CC110_BG_W 466\n')
    f.write('#define CC110_BG_H 466\n')
    f.write('const uint16_t CC110_BG[CC110_BG_W * CC110_BG_H] = {\n')
    for i in range(0, len(bg565), 12):
        f.write(','.join('0x%04X' % v for v in bg565[i:i + 12]) + ',\n')
    f.write('};\n')
print('Cc110Background.h OK, %dKB' % (len(bg565) * 2 // 1024))

# ---------------- 3. 预览截图 ----------------
def c565(v):
    r = (v >> 11) & 0x1F; g = (v >> 5) & 0x3F; b = v & 0x1F
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)

# 用量化后的 565 色还原，预览即屏幕真实效果
prev = Image.new('RGB', (466, 466))
pp = prev.load()
i = 0
for y in range(466):
    for x in range(466):
        pp[x, y] = c565(bg565[i]); i += 1
d = ImageDraw.Draw(prev, 'RGBA')

BLACK, WHITE = (0, 0, 0), (255, 255, 255)
GREEN, GREY, CYAN = c565(0x07E0), c565(0x7BEF), c565(0x07FF)
CX = 233
FONT_DIR = r'C:\Windows\Fonts'

def font(px_size):
    for name in ('consola.ttf', 'arial.ttf'):
        try:
            return ImageFont.truetype(os.path.join(FONT_DIR, name), px_size)
        except Exception:
            pass
    return ImageFont.load_default()

def cjkfont(px_size):
    for name in ('Noto Sans SC (TrueType).otf', 'msyh.ttc', 'simsun.ttc', 'simhei.ttf'):
        try:
            return ImageFont.truetype(os.path.join(FONT_DIR, name), px_size)
        except Exception:
            pass
    return ImageFont.load_default()

# 时钟区（drawHomeClock 同位置：富士山顶上方天空处，透明底直接叠在背景图上；无黑块、无农历）
t8 = font(58)  # size8: 6x8/字 -> 48x64/字，Consolas 近似
tw = d.textlength('20:31', font=t8)
d.text((CX - tw / 2, 40), '20:31', font=t8, fill=WHITE)
t2 = font(15)
# 公历日期：yyyy/mm/dd 格式，加粗居中、白色（与固件 printCenteredBold 一致，横向/纵向各偏移1px模拟粗体）
dw = d.textlength('2026/10/29', font=t2)
date_x = CX - dw / 2
for dx, dy in ((0, 0), (1, 0), (0, 1), (1, 1)):
    d.text((date_x + dx, 110 + dy), '2026/10/29', font=t2, fill=WHITE)

# 连接状态（日期下方天空处，颜色不变：ready 绿 / no link 灰 / LINK LOST 红）
t2b = font(14)
st_txt, st_col = 'ready', GREEN
wpx = d.textlength(st_txt, font=t2b)
d.text((CX - wpx / 2, 134 + 1), st_txt, font=t2b, fill=st_col)

# 蓝牙名 + 固件版本合成一行（y=430：原 y=452 两端会被圆屏裁掉；轮子下方地面位置）
tf = font(9)
fw = 'MotoNav-1C2D  fw v7.18.0-143c  40MHz'
wpx = d.textlength(fw, font=tf)
d.text((CX - wpx / 2, 430), fw, font=tf, fill=GREY)

# 电池 + 蓝牙点：整组放大一倍（s=2，W=48 H=24），上移到 y0=400，与固件 drawStatusBottom 同位置（透明底）
pct = 80
bc = CYAN                                                        # 充电中：青色
s = 2
W, H = 24 * s, 12 * s
d.ellipse([CX - 58 - 7, 412 - 7, CX - 58 + 7, 412 + 7], fill=GREEN)  # 蓝牙点（整组左侧，y0+12=412）
bx, by = CX - 47, 400                                            # 电池图标（2 倍放大）
d.rounded_rectangle([bx, by, bx + W, by + H], 3 * s, outline=bc, width=2 * s)
d.rectangle([bx + 3 * s, by + 3 * s, bx + 3 * s + (W - 6 * s) * pct // 100, by + H - 3 * s], fill=bc)
d.text((bx + W + 5 * s, by + 2 * s), str(pct), font=font(9 * s), fill=bc)

# 页面指示点（drawPageDots 同位置：y=444，间距20）
for i2 in range(3):
    col = WHITE if i2 == 1 else (68, 68, 68)   # 主页在中间
    x = CX + (i2 - 1) * 20
    d.ellipse([x - 4, 444 - 4, x + 4, 444 + 4], fill=col)

prev.save(PREVIEW)
print('preview saved:', PREVIEW)

# 圆屏模拟：按 466x466 圆形遮罩裁切（AMOLED 圆屏实际可视区域），四角外区域透明
ROUND = os.path.join(os.path.dirname(PREVIEW), 'preview_cc110_round.png')
mask = Image.new('L', (466, 466), 0)
ImageDraw.Draw(mask).ellipse([0, 0, 465, 465], fill=255)
rprev = prev.convert('RGBA')
rprev.putalpha(mask)
rprev.save(ROUND)
print('round preview saved:', ROUND)
