# -*- coding: utf-8 -*-
# 生成 resources/LunarFont.h：
#  - 农历用到的汉字（16x16 单色位图），由系统 CJK 字体栅格化得到
#  - 内置 solar2lunar / 农历格式化，供固件直接调用
# 运行：python gen_lunar_font.py
import os, datetime
from PIL import Image, ImageDraw, ImageFont

BASE = r"C:\Users\William Koo\WorkBuddy\2026-10-24-23-23-18\MotoNav\firmware\boards\143\MotoNav"
RES = os.path.join(BASE, "resources")
OUT = os.path.join(RES, "LunarFont.h")

# 农历显示所需汉字及其 Unicode（新增“月”）
GLYPHS = {
    "正": 0x6B63, "二": 0x4E8C, "三": 0x4E09, "四": 0x56DB, "五": 0x4E94,
    "六": 0x516D, "七": 0x4E03, "八": 0x516B, "九": 0x4E5D, "十": 0x5341,
    "冬": 0x51AC, "腊": 0x814A, "闰": 0x95F0, "初": 0x521D, "廿": 0x5EFF,
    "月": 0x6708,
}
SZ = 16
FONT = r"C:\Windows\Fonts\Noto Sans SC (TrueType).otf"

fnt = ImageFont.truetype(FONT, 48)
names = list(GLYPHS.keys())
cps, bitmaps = [], []
for ch in names:
    img = Image.new("L", (SZ * 3, SZ * 3), 0)
    ImageDraw.Draw(img).text((SZ * 1.5, SZ * 1.5), ch, font=fnt, fill=255, anchor="mm")
    img = img.resize((SZ, SZ), Image.LANCZOS).point(lambda p: 255 if p > 128 else 0)
    px = img.load()
    rows = []
    for r in range(SZ):
        b0 = b1 = 0
        for c in range(8):
            if px[c, r]: b0 |= 0x80 >> c
        for c in range(8, 16):
            if px[c, r]: b1 |= 0x80 >> (c - 8)
        rows.append(b0); rows.append(b1)
    bitmaps.append(rows)
    cps.append(GLYPHS[ch])

# ---------------- 农历换算（1900-2099） ----------------
LUNAR_INFO = [
0x04bd8,0x04ae0,0x0a570,0x054d5,0x0d260,0x0d950,0x16554,0x056a0,0x09ad0,0x055d2,
0x04ae0,0x0a5b6,0x0a4d0,0x0d250,0x1d255,0x0b540,0x0d6a0,0x0ada2,0x095b0,0x14977,
0x04970,0x0a4b0,0x0b4b5,0x06a50,0x06d40,0x1ab54,0x02b60,0x09570,0x052f2,0x04970,
0x06566,0x0d4a0,0x0ea50,0x06e95,0x05ad0,0x02b60,0x186e3,0x092e0,0x1c8d7,0x0c950,
0x0d4a0,0x1d8a6,0x0b550,0x056a0,0x1a5b4,0x025d0,0x092d0,0x0d2b2,0x0a950,0x0b557,
0x06ca0,0x0b550,0x15355,0x04da0,0x0a5b0,0x14573,0x052b0,0x0a9a8,0x0e950,0x06aa0,
0x0aea6,0x0ab50,0x04b60,0x0aae4,0x0a570,0x05260,0x0f263,0x0d950,0x05b57,0x056a0,
0x096d0,0x04dd5,0x04ad0,0x0a4d0,0x0d4d4,0x0d250,0x0d558,0x0b540,0x0b6a0,0x195a6,
0x095b0,0x049b0,0x0a974,0x0a4b0,0x0b27a,0x06a50,0x06d40,0x0af46,0x0ab60,0x09570,
0x04af5,0x04970,0x064b0,0x074a3,0x0ea50,0x06b58,0x055c0,0x0ab60,0x096d5,0x092e0,
0x0c960,0x0d954,0x0d4a0,0x0da50,0x07552,0x056a0,0x0abb7,0x025d0,0x092d0,0x0cab5,
0x0a950,0x0b4a0,0x0baa4,0x0ad50,0x055d9,0x04ba0,0x0a5b0,0x15176,0x052b0,0x0a930,
0x07954,0x06aa0,0x0ad50,0x05b52,0x04b60,0x0a6e6,0x0a4e0,0x0d260,0x0ea65,0x0d530,
0x05aa0,0x076a3,0x096d0,0x04afb,0x04ad0,0x0a4d0,0x1d0b6,0x0d250,0x0d520,0x0dd45,
0x0b5a0,0x056d0,0x055b2,0x049b0,0x0a577,0x0a4b0,0x0aa50,0x1b255,0x06d20,0x0ada0,
0x14b63,0x09370,0x049f8,0x04970,0x064b0,0x168a6,0x0ea50,0x06b20,0x1a6c4,0x0aae0,
0x0a2e0,0x0d2e3,0x0c960,0x0d557,0x0d4a0,0x0da50,0x05d55,0x056a0,0x0a6d0,0x055d4,
0x052d0,0x0a9b8,0x0a950,0x0b4a0,0x0b6a6,0x0ad50,0x055a0,0x0aba4,0x0a5b0,0x052b0,
0x0b273,0x06930,0x07337,0x06aa0,0x0ad50,0x14b55,0x04b60,0x0a570,0x054e4,0x0d160,
0x0e968,0x0d520,0x0daa0,0x16aa6,0x056d0,0x04ae0,0x0a9d4,0x0a2d0,0x0d150,0xf252,
0x0d520,
]

def l_year_days(y):
    info = LUNAR_INFO[y - 1900]; s = 348; i = 0x8000
    while i > 0x8:
        if info & i: s += 1
        i >>= 1
    if info & 0xf: s += 30 if (info & 0x10000) else 29
    return s
def leap_month(y): return LUNAR_INFO[y - 1900] & 0xf
def leap_days(y):
    if leap_month(y): return 30 if (LUNAR_INFO[y - 1900] & 0x10000) else 29
    return 0
def month_days(y, m): return 30 if (LUNAR_INFO[y - 1900] & (0x10000 >> m)) else 29

def solar2lunar(y, m, d):
    base = datetime.date(1900, 1, 31)
    offset = (datetime.date(y, m, d) - base).days
    i = 1900; temp = 0; is_leap = False
    while i < 2101 and offset > 0:
        temp = l_year_days(i); offset -= temp; i += 1
    if offset < 0:
        offset += temp; i -= 1
    ly = i; leap = leap_month(ly); i = 1
    while i < 13 and offset > 0:
        if leap > 0 and i == (leap + 1) and not is_leap:
            i -= 1; is_leap = True; temp = leap_days(ly)
        else:
            temp = month_days(ly, i)
        if is_leap and i == (leap + 1): is_leap = False
        offset -= temp; i += 1
    if offset == 0 and leap > 0 and i == leap + 1:
        if is_leap: is_leap = False
        else: is_leap = True; i -= 1
    if offset < 0:
        offset += temp; i -= 1
    return ly, i, offset + 1, is_leap

def lunar_str(y, m, d):
    ly, lm, ld, leap = solar2lunar(y, m, d)
    nm = ["正","二","三","四","五","六","七","八","九","十","冬","腊"]
    ones = ["一","二","三","四","五","六","七","八","九","十"]
    s = ("闰" if leap else "") + nm[lm - 1] + "月"
    if ld <= 10: s += "初" + ones[ld - 1]
    elif ld < 20: s += "十" + ones[ld - 11]
    elif ld == 20: s += "二十"
    elif ld < 30: s += "廿" + ones[ld - 21]
    else: s += "三十"
    return s

print("--- lunar self-test ---")
for y, m, d in [(2026,2,17),(2026,10,29),(2026,1,1),(2026,9,25),(2025,1,29),(2033,1,1)]:
    ly, lm, ld, lp = solar2lunar(y, m, d)
    print((y,m,d), "->", lunar_str(y,m,d), "(ly=%d lm=%d ld=%d leap=%s)" % (ly, lm, ld, lp))

# ---------------- 写出 C 头文件 ----------------
N = len(names)
with open(OUT, "w", encoding="utf-8") as f:
    f.write("#pragma once\n")
    f.write("#include <stdint.h>\n#include <string.h>\n")
    f.write("// 农历：%d 个汉字 16x16 单色位图 + 公历->农历换算。由 gen_lunar_font.py 自动生成。\n" % N)
    f.write("#define LUNAR_GLYPH_W 16\n#define LUNAR_GLYPH_H 16\n\n")
    f.write("static const uint32_t lunarCp[%d] = {" % N)
    f.write(",".join("0x%04X" % GLYPHS[c] for c in names))
    f.write("};\n\n")
    f.write("static const uint8_t lunarBmp[%d][32] = {\n" % N)
    for rows in bitmaps:
        f.write("  {" + ",".join("0x%02X" % b for b in rows) + "},\n")
    f.write("};\n\n")

    f.write("static const uint32_t LUNAR_INFO[201] = {\n")
    for i in range(0, 201, 5):
        f.write("  " + ",".join("0x%04X" % v for v in LUNAR_INFO[i:i+5]) + ",\n")
    f.write("};\n\n")

    f.write("static int lYearDays(int y){\n")
    f.write("  int info=LUNAR_INFO[y-1900],s=348,i=0x8000;\n")
    f.write("  for(;i>0x8;i>>=1) if(info&i) s++;\n")
    f.write("  if(info&0xf) s += (info&0x10000)?30:29;\n  return s;\n}\n")
    f.write("static int leapMonth(int y){ return LUNAR_INFO[y-1900]&0xf; }\n")
    f.write("static int leapDays(int y){ int lm=leapMonth(y); if(!lm) return 0; return (LUNAR_INFO[y-1900]&0x10000)?30:29; }\n")
    f.write("static int monthDays(int y,int m){ return (LUNAR_INFO[y-1900]&(0x10000>>m))?30:29; }\n\n")

    f.write("static void solar2lunar(int y,int m,int d,int &ly,int &lm,int &ld,bool &isLeap){\n")
    f.write("  static const int md[]={31,28,31,30,31,30,31,31,30,31,30,31};\n")
    f.write("  auto days=[&](int yy,int mm,int dd){\n")
    f.write("    int s=0; for(int i=1900;i<yy;++i) s+=(i%4==0&&(i%100!=0||i%400==0))?366:365;\n")
    f.write("    for(int i=1;i<mm;++i) s+=md[i-1]+(i==2&&(yy%4==0&&(yy%100!=0||yy%400==0))?1:0);\n")
    f.write("    return s+dd-1;\n  };\n")
    f.write("  int offset=days(y,m,d)-days(1900,1,31);\n")
    f.write("  int i=1900,temp=0; isLeap=false;\n")
    f.write("  for(; i<2101 && offset>0; ++i){ temp=lYearDays(i); offset-=temp; }\n")
    f.write("  if(offset<0){ offset+=temp; --i; }\n")
    f.write("  ly=i; int leap=leapMonth(ly); i=1;\n")
    f.write("  for(; i<13 && offset>0; ++i){\n")
    f.write("    if(leap>0 && i==(leap+1) && !isLeap){ --i; isLeap=true; temp=leapDays(ly); }\n")
    f.write("    else { temp=monthDays(ly,i); }\n")
    f.write("    if(isLeap && i==(leap+1)) isLeap=false;\n    offset-=temp;\n  }\n")
    f.write("  if(offset==0 && leap>0 && i==leap+1){ if(isLeap) isLeap=false; else { isLeap=true; --i; } }\n")
    f.write("  if(offset<0){ offset+=temp; --i; }\n  lm=i; ld=offset+1;\n}\n\n")

    f.write("// 农历格式化：闰X月 + 日（初/十/廿）。out 至少 24 字节。\n")
    f.write("static void formatLunar(int m,bool leap,int d,char *out){\n")
    f.write("  static const char* mn[12]={\"\xE6\xAD\xA3\",\"\xE4\xBA\x8C\",\"\xE4\xB8\x89\",\"\xE5\x9B\x9B\",\"\xE4\xBA\x94\",\"\xE5\x85\xAD\",\"\xE4\xB8\x83\",\"\xE5\x85\xAB\",\"\xE4\xB9\x9D\",\"\xE5\x8D\x81\",\"\xE5\x86\xAC\",\"\xE8\x85\x8A\"};\n")
    f.write("  static const char* ones[10]={\"\xE4\xB8\x80\",\"\xE4\xBA\x8C\",\"\xE4\xB8\x89\",\"\xE5\x9B\x9B\",\"\xE4\xBA\x94\",\"\xE5\x85\xAD\",\"\xE4\xB8\x83\",\"\xE5\x85\xAB\",\"\xE4\xB9\x9D\",\"\xE5\x8D\x81\"};\n")
    f.write("  static const char yue[4]=\"\\xE6\\x9C\\x88\";\n")
    f.write("  int o=0;\n")
    f.write("  if(leap){ memcpy(out+o,\"\\xE9\\x98\\x8D\",3); o+=3; }\n")
    f.write("  memcpy(out+o,mn[m-1],3); o+=3;\n  memcpy(out+o,yue,3); o+=3;\n")
    f.write("  if(d<=10){ memcpy(out+o,\"\\xE5\\x88\\x9D\",3); memcpy(out+o+3,ones[d-1],3); o+=6; }\n")
    f.write("  else if(d<20){ memcpy(out+o,\"\\xE5\\x8D\\x81\",3); memcpy(out+o+3,ones[d-11],3); o+=6; }\n")
    f.write("  else if(d==20){ memcpy(out+o,\"\\xE4\\xBA\\x8C\\xE5\\x8D\\x81\",6); o+=6; }\n")
    f.write("  else if(d<30){ memcpy(out+o,\"\\xE5\\xBB\\xBF\",3); memcpy(out+o+3,ones[d-21],3); o+=6; }\n")
    f.write("  else { memcpy(out+o,\"\\xE4\\xB8\\x89\\xE5\\x8D\\x81\",6); o+=6; }\n  out[o]=0;\n}\n\n")

    f.write("static void drawLunarGlyph(uint32_t cp,int x,int y,uint16_t c){\n")
    f.write("  int idx=-1; for(int i=0;i<%d;++i) if(lunarCp[i]==cp){idx=i;break;}\n" % N)
    f.write("  if(idx<0) return;\n  const uint8_t* b=lunarBmp[idx];\n")
    f.write("  for(int r=0;r<16;++r){\n")
    f.write("    uint8_t row=b[r]; for(int cc=0;cc<8;++cc) if(row&(0x80>>cc)) gfx->drawPixel(x+cc,y+r,c);\n")
    f.write("    uint8_t row2=b[16+r]; for(int cc=0;cc<8;++cc) if(row2&(0x80>>cc)) gfx->drawPixel(x+8+cc,y+r,c);\n")
    f.write("  }\n}\n\n")
    f.write("// 绘制 UTF-8 农历串；每个汉字 16x16，逐字右移。x,y 为左上角。\n")
    f.write("static void drawLunar(const char* s,int x,int y,uint16_t c){\n")
    f.write("  while(*s){\n    uint32_t cp=0; int n=0;\n")
    f.write("    if(!(*s&0x80)){ cp=*s++; n=1; }\n")
    f.write("    else if((*s&0xE0)==0xC0){ cp=((s[0]&0x1F)<<6)|(s[1]&0x3F); s+=2; n=2; }\n")
    f.write("    else if((*s&0xF0)==0xE0){ cp=((s[0]&0x0F)<<12)|((s[1]&0x3F)<<6)|(s[2]&0x3F); s+=3; n=3; }\n")
    f.write("    else { s++; continue; }\n    if(n==0) break;\n    drawLunarGlyph(cp,x,y,c); x+=16;\n  }\n}\n")

print("written:", OUT, "glyphs:", N)
