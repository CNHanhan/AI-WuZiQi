#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 AI-WuZiQi 应用图标（五子棋主题：棋盘格 + 黑白棋子），输出 .ico 与 .png"""
from PIL import Image, ImageDraw
import os

OUT = r"C:\Users\Han\Documents\QT\AI-WuZiQi\client\resources"
S = 256
im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(im)

# 圆角深木色底
d.rounded_rectangle([8, 8, S-8, S-8], radius=48, fill=(92, 58, 33, 255),
                    outline=(45, 28, 16, 255), width=6)

# 棋盘网格 (5x5 示意，留边距)
m = 42
step = (S - 2*m) // 4
for i in range(5):
    p = m + i*step
    d.line([(p, m), (p, m+4*step)], fill=(210, 175, 130, 160), width=3)
    d.line([(m, p), (m+4*step, p)], fill=(210, 175, 130, 160), width=3)

# 星位
for (gx, gy) in [(2,2)]:
    cx, cy = m + gx*step, m + gy*step
    d.ellipse([cx-5, cy-5, cx+5, cy+5], fill=(210, 175, 130, 200))

# 黑子（左上）
def stone(cx, cy, color, hi):
    r = step//2 - 6
    d.ellipse([cx-r, cy-r, cx+r, cy+r], fill=color, outline=(0,0,0,120), width=3)
    d.ellipse([cx-r+6, cy-r+6, cx-r+22, cy-r+22], fill=hi)

b1 = (m+step, m+step)          # 黑 (1,1)
b2 = (m+2*step, m+2*step)      # 黑 (2,2)
b3 = (m+3*step, m+step)        # 黑 (3,1)
w1 = (m+2*step, m+step)        # 白 (2,1)
w2 = (m+3*step, m+2*step)      # 白 (3,2)
for (x, y) in (b1, b2, b3):
    stone(x, y, (24, 24, 28, 255), (110, 110, 120, 180))
for (x, y) in (w1, w2):
    stone(x, y, (245, 245, 245, 255), (255, 255, 255, 230))

png = os.path.join(OUT, "appicon.png")
ico = os.path.join(OUT, "appicon.ico")
im.save(png, "PNG")
im.save(ico, "ICO", sizes=[(256,256),(128,128),(64,64),(48,48),(32,32),(16,16)])
print("PNG", png, os.path.getsize(png))
print("ICO", ico, os.path.getsize(ico))
