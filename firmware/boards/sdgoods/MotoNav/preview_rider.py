# -*- coding: utf-8 -*-
# 生成"模式1 待机画面"圆屏模拟预览：
#   黑底 + cc110 摩托(motorcycle_running.gif 某帧) + 点赞小人(thumbs_up.gif 末帧) + 与模式0完全一致的文字层。
import os
from PIL import Image, ImageDraw, ImageFont

BASE = r"C:\Users\William Koo\WorkBuddy\2026-10-24-23-23-18\MotoNav\firmware\boards\143\MotoNav"
OUT = os.path.join(BASE, "preview_rider.png")
ROUND = os.path.join(BASE, "preview_rider_round.png")
FONT_DIR = r'C:\Windows\Fonts'

CX = 233
THUMB_W, THUMB_H = 100, 160
MOTO_W, MOTO_H = 180, 116
ANIM_GAP = 6
ANIM_CX = CX
THUMB_X0 = ANIM_CX - (THUMB_W + ANIM_GAP + MOTO_W) // 2          # 左侧点赞小人
MOTO_X0 = THUMB_X0 + THUMB_W + ANIM_GAP                          # 右侧摩托
ANIM_BOT = 343
THUMB_Y0 = ANIM_BOT - THUMB_H                                   # 183
MOTO_Y0 = ANIM_BOT - MOTO_H - 12                                # 215
THRESH = 248  # 摩托白底阈值

def font(px):
    for n in ('consola.ttf', 'arial.ttf'):
        try: return ImageFont.truetype(os.path.join(FONT_DIR, n), px)
        except Exception: pass
    return ImageFont.load_default()

img = Image.new('RGB', (466, 466), (0, 0, 0))

# 摩托：取首帧（动画起始姿态），白底透明
mg = Image.open(os.path.join(BASE, "motorcycle_running.gif"))
mg.seek(0)
m = mg.convert('RGBA').resize((MOTO_W, MOTO_H), Image.LANCZOS)
mp = m.load()
for y in range(MOTO_H):
    for x in range(MOTO_W):
        r, g, b, a = mp[x, y]
        if r >= THRESH and g >= THRESH and b >= THRESH:
            mp[x, y] = (r, g, b, 0)
img.paste(m, (MOTO_X0, MOTO_Y0), m)

# 点赞小人：取末帧（点赞完成、停顿 3 秒所定格的姿态）
tg = Image.open(os.path.join(BASE, "thumbs_up.gif"))
n = getattr(tg, 'n_frames', 1)
tg.seek(n - 1)
t = tg.convert('RGBA').resize((THUMB_W, THUMB_H), Image.LANCZOS)
img.paste(t, (THUMB_X0, THUMB_Y0), t)

d = ImageDraw.Draw(img, 'RGBA')
WHITE = (255, 255, 255); GREEN = (0, 200, 0); GREY = (130, 130, 130)
# 时钟
t8 = font(58); tw = d.textlength('20:31', font=t8); d.text((CX - tw/2, 40), '20:31', font=t8, fill=WHITE)
# 日期 yyyy/mm/dd 加粗居中白字
t2 = font(15); dw = d.textlength('2026/10/30', font=t2); dx0 = CX - dw/2
for dx, dy in ((0, 0), (1, 0), (0, 1), (1, 1)):
    d.text((dx0 + dx, 110 + dy), '2026/10/30', font=t2, fill=WHITE)
# ready
t2b = font(14); wpx = d.textlength('ready', font=t2b); d.text((CX - wpx/2, 135), 'ready', font=t2b, fill=GREEN)
# 版本号一行
tf = font(9); fw = 'MotoNav-1C2D  fw v7.18.0-143c  40MHz'; wpx = d.textlength(fw, font=tf)
d.text((CX - wpx/2, 430), fw, font=tf, fill=GREY)
# 电池 2 倍（青色充电）
CYAN = (0, 200, 200); s = 2; W, H = 24*s, 12*s
d.ellipse([CX - 58 - 7, 412 - 7, CX - 58 + 7, 412 + 7], fill=GREEN)
bx, by = CX - 32, 400
d.rounded_rectangle([bx, by, bx + W, by + H], 3*s, outline=CYAN, width=2*s)
d.rectangle([bx + 3*s, by + 3*s, bx + 3*s + (W - 6*s)*80//100, by + H - 3*s], fill=CYAN)
d.text((bx + W + 5*s, by + 2*s), '80', font=font(9*s), fill=CYAN)
# 页面指示点
for i in range(3):
    col = WHITE if i == 1 else (68, 68, 68)
    x = CX + (i - 1)*20; d.ellipse([x - 4, 444 - 4, x + 4, 444 + 4], fill=col)

img.save(OUT); print('saved', OUT, 'thumb frames', n, 'used thumb frame', n - 1, 'moto frame 0')

mask = Image.new('L', (466, 466), 0)
ImageDraw.Draw(mask).ellipse([0, 0, 465, 465], fill=255)
ip = img.convert('RGBA'); ip.putalpha(mask); ip.save(ROUND)
print('round saved', ROUND)
