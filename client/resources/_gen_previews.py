# -*- coding: utf-8 -*-
"""为 AI-WuZiQi 商城生成 11 张商品预览图（纯 Pillow 绘制，无外网依赖）。
生成后本脚本可删除；产物位于 client/resources/res/ 下。
"""
import os, math, random
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(ROOT, "res")
W, H = 360, 270

def ensure(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    return path

def grid(draw, pad=48, n=9, color=(20, 20, 20, 150)):
    step = (W - 2 * pad) / (n - 1)
    for i in range(n):
        p = pad + i * step
        draw.line([(p, pad), (p, H - pad)], fill=color, width=2)
        draw.line([(pad, p), (W - pad, p)], fill=color, width=2)
    # 星位
    for gx in (2, 4, 6):
        for gy in (2, 4, 6):
            x, y = pad + gx * step, pad + gy * step
            r = 3
            draw.ellipse([x - r, y - r, x + r, y + r], fill=color)

def vignette(img, strength=0.35):
    """四周压暗，提升质感。"""
    overlay = Image.new("L", (W, H), 0)
    od = ImageDraw.Draw(overlay)
    maxr = math.hypot(W / 2, H / 2)
    steps = 40
    for i in range(steps):
        r = maxr * (1 - i / steps)
        a = int(255 * strength * (i / steps) ** 2)
        od.ellipse([W / 2 - r, H / 2 - r, W / 2 + r, H / 2 + r], outline=a, width=2)
    overlay = overlay.filter(ImageFilter.GaussianBlur(18))
    dark = Image.new("RGB", (W, H), (0, 0, 0))
    return Image.composite(dark, img, overlay)

def sphere(img, cx, cy, r, base, light, dark):
    """画一个带高光的球体（棋子）。"""
    cx, cy, r = int(cx), int(cy), int(r)
    s = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(s)
    for yy in range(cy - r, cy + r + 1):
        for xx in range(cx - r, cx + r + 1):
            dx, dy = xx - cx, yy - cy
            dist2 = dx * dx + dy * dy
            if dist2 > r * r:
                continue
            dist = math.sqrt(dist2)
            # 法线 z
            nz = math.sqrt(max(0.0, r * r - dist2)) / r
            # 光照方向（左上）
            lx, ly, lz = -0.5, -0.6, 0.62
            diff = max(0.0, (-dx / r) * lx + (-dy / r) * ly + nz * lz)
            spec = max(0.0, diff) ** 28
            shade = 0.25 + 0.75 * diff
            col = tuple(int(base[k] * shade + (light[k] - base[k]) * spec) for k in range(3))
            # 边缘压暗
            edge = 1.0 - 0.45 * (dist / r) ** 6
            col = tuple(int(c * edge) for c in col)
            d.point((xx, yy), fill=(col[0], col[1], col[2], 255))
    # 玻璃高光斑
    d.ellipse([cx - r * 0.55, cy - r * 0.6, cx - r * 0.1, cy - r * 0.2],
              fill=(255, 255, 255, 110))
    img.paste(s, (0, 0), s)

def label(img, text):
    d = ImageDraw.Draw(img)
    font = None
    for fp in ("C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/simhei.ttf"):
        if os.path.exists(fp):
            try:
                font = ImageFont.truetype(fp, 26)
                break
            except Exception:
                pass
    if font is None:
        font = ImageFont.load_default()
    tw = d.textbbox((0, 0), text, font=font)[2]
    d.rounded_rectangle([(W - tw - 30, H - 50), (W - 12, H - 12)], radius=10,
                        fill=(0, 0, 0, 130))
    d.text((W - tw - 21, H - 46), text, font=font, fill=(255, 255, 255, 235))

def save_png(img, rel):
    p = ensure(os.path.join(RES, rel))
    img.convert("RGB").save(p, "PNG", optimize=True)

def save_webp_and_png(img, rel_webp, rel_png):
    pw = ensure(os.path.join(RES, rel_webp))
    pp = ensure(os.path.join(RES, rel_png))
    rgb = img.convert("RGB")
    rgb.save(pw, "WEBP", quality=82)
    rgb.save(pp, "PNG", optimize=True)

# ---------- 棋盘 ----------
# 经典木纹
random.seed(1)
bg = Image.new("RGB", (W, H), (150, 100, 55))
d = ImageDraw.Draw(bg)
for i in range(H):
    t = i / H
    w = int(4 * math.sin(t * 18) + 3 * math.sin(t * 41))
    c = (int(150 + 25 * math.sin(t * 9)), int(100 + 18 * math.sin(t * 7)), int(55 + 12 * math.sin(t * 5)))
    d.line([(0, i), (W, i)], fill=c, width=1)
for _ in range(30):
    x = random.randint(0, W); y = random.randint(0, H)
    d.arc([x - 30, y - 8, x + 30, y + 8], 180, 360, fill=(120, 78, 42), width=1)
bg = vignette(bg, 0.3)
ov = Image.new("RGBA", (W, H), (0, 0, 0, 0)); grid(ImageDraw.Draw(ov), color=(60, 35, 18, 180))
bg = Image.alpha_composite(bg.convert("RGBA"), ov); label(bg, "经典棋盘")
save_png(bg, "board/classic.png")

# 玻璃棋盘
bg = Image.new("RGB", (W, H))
pix = bg.load()
for y in range(H):
    for x in range(W):
        t = (x + y) / (W + H)
        pix[x, y] = (int(190 + 40 * math.sin(t * 6)), int(220 + 30 * math.sin(t * 5)), 245)
d = ImageDraw.Draw(bg)
for i in range(0, W, 40):
    d.line([(i, 0), (i + 120, H)], fill=(255, 255, 255, 60), width=6)
bg = vignette(bg, 0.25)
ov = Image.new("RGBA", (W, H), (0, 0, 0, 0)); grid(ImageDraw.Draw(ov), color=(30, 90, 140, 170))
bg = Image.alpha_composite(bg.convert("RGBA"), ov); label(bg, "玻璃棋盘")
save_png(bg, "board/glass.png")

# 星空棋盘
random.seed(7)
bg = Image.new("RGB", (W, H))
pix = bg.load()
for y in range(H):
    for x in range(W):
        t = y / H
        pix[x, y] = (int(10 + 20 * t), int(12 + 16 * t), int(40 + 55 * t))
d = ImageDraw.Draw(bg)
for _ in range(220):
    x = random.randint(0, W); y = random.randint(0, H); r = random.choice([0, 0, 1, 1, 2])
    b = random.randint(150, 255)
    d.ellipse([x - r, y - r, x + r, y + r], fill=(b, b, min(255, b + 20)))
ov = Image.new("RGBA", (W, H), (0, 0, 0, 0)); grid(ImageDraw.Draw(ov), color=(120, 160, 255, 110))
bg = Image.alpha_composite(bg.convert("RGBA"), ov); label(bg, "星空棋盘")
save_png(bg, "board/galaxy.png")

# ---------- 棋子 ----------
# 玻璃棋子
img = Image.new("RGB", (W, H), (28, 40, 55))
d = ImageDraw.Draw(img)
d.ellipse([W/2-95, H/2-95+8, W/2+95, H/2+95+8], fill=(0, 0, 0))
sphere(img, W/2, H/2, 88, (150, 205, 235), (245, 252, 255), (40, 90, 130))
label(img, "玻璃棋子")
save_png(img, "stone/glass.png")

# 玛瑙棋子
img = Image.new("RGB", (W, H), (45, 30, 28))
d = ImageDraw.Draw(img); d.ellipse([W/2-95, H/2-95+8, W/2+95, H/2+95+8], fill=(0, 0, 0))
sphere(img, W/2, H/2, 88, (170, 70, 55), (255, 200, 170), (90, 25, 20))
# 玛瑙纹路
d = ImageDraw.Draw(img)
for k in range(5):
    off = -40 + k * 20
    d.arc([W/2-80, H/2-70+off, W/2+80, H/2-10+off], 200, 340, fill=(255, 200, 180, 90), width=3)
label(img, "玛瑙棋子")
save_png(img, "stone/agate.png")

# 玉石棋子
img = Image.new("RGB", (W, H), (24, 45, 38))
d = ImageDraw.Draw(img); d.ellipse([W/2-95, H/2-95+8, W/2+95, H/2+95+8], fill=(0, 0, 0))
sphere(img, W/2, H/2, 88, (70, 175, 120), (220, 255, 230), (20, 85, 55))
label(img, "玉石棋子")
save_png(img, "stone/jade.png")

# ---------- 特效 ----------
# 落子流光
random.seed(11)
img = Image.new("RGB", (W, H), (8, 12, 30))
glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
gd = ImageDraw.Draw(glow)
for i in range(9):
    x0 = 30 + i * 38
    pts = [(x0, H), (x0 + random.randint(-30, 30), H - 90),
           (W/2 + random.randint(-60, 60), H/2), (W/2, 40)]
    gd.line(pts, fill=(80 + i * 15, 180, 255, 200), width=4)
glow = glow.filter(ImageFilter.GaussianBlur(6))
img = Image.alpha_composite(img.convert("RGBA"), glow)
d = ImageDraw.Draw(img)
d.ellipse([W/2-22, H/2-22, W/2+22, H/2+22], fill=(235, 250, 255, 255))
label(img, "落子流光")
save_webp_and_png(img, "effect/glow.webp", "effect/glow.png")

# 五连烟花
random.seed(23)
img = Image.new("RGB", (W, H), (6, 8, 22))
for cx, cy, col in [(90, 90, (255, 120, 160)), (W/2, 70, (120, 220, 255)), (270, 100, (255, 210, 90))]:
    for i in range(70):
        a = 2 * math.pi * i / 70
        rr = random.randint(40, 92)
        x = cx + math.cos(a) * rr; y = cy + math.sin(a) * rr
        d = ImageDraw.Draw(img)
        d.ellipse([x-2, y-2, x+2, y+2], fill=tuple(min(255, c + random.randint(-20, 20)) for c in col))
    d = ImageDraw.Draw(img)
    d.ellipse([cx-6, cy-6, cx+6, cy+6], fill=(255, 255, 255))
img = img.filter(ImageFilter.GaussianBlur(0.6))
label(img, "五连烟花")
save_webp_and_png(img, "effect/firework.webp", "effect/firework.png")

# ---------- 头像 ----------
# 默认头像
img = Image.new("RGB", (W, H), (215, 220, 228))
d = ImageDraw.Draw(img)
d.ellipse([W/2-60, 45, W/2+60, 165], fill=(150, 160, 172))
d.ellipse([W/2-100, 175, W/2+100, 340], fill=(150, 160, 172))
label(img, "默认头像")
save_png(img, "avatar/default.png")

# 冷咖啡头像
img = Image.new("RGB", (W, H), (60, 48, 42))
d = ImageDraw.Draw(img)
d.rounded_rectangle([110, 80, 250, 210], radius=16, fill=(235, 232, 222))
d.ellipse([110, 80, 250, 130], fill=(120, 78, 50))
d.arc([250, 110, 300, 175], 270, 90, fill=(235, 232, 222), width=12)
for i in range(3):
    x = 150 + i * 30
    d.line([(x, 70), (x - 12, 35)], fill=(200, 200, 200), width=3)
label(img, "冷咖啡")
save_png(img, "avatar/coldcoffee.png")

# 富江头像
img = Image.new("RGB", (W, H), (30, 18, 26))
d = ImageDraw.Draw(img)
# 长发
d.ellipse([W/2-80, 50, W/2+80, 240], fill=(18, 12, 16))
# 脸
d.ellipse([W/2-52, 70, W/2+52, 175], fill=(238, 214, 205))
# 眼
d.line([W/2-38, 120, W/2-12, 120], fill=(20, 10, 14), width=4)
d.line([W/2+12, 120, W/2+38, 120], fill=(20, 10, 14), width=4)
d.ellipse([W/2-6, 118, W/2+6, 130], fill=(15, 8, 12))
# 唇
d.arc([W/2-16, 140, W/2+16, 162], 0, 180, fill=(170, 30, 45), width=4)
# 红衣
d.polygon([(W/2-70, 270), (W/2+70, 270), (W/2+45, 185), (W/2-45, 185)], fill=(150, 25, 40))
label(img, "富江")
save_png(img, "avatar/fujiang.png")

print("DONE: generated previews under", RES)
for root, _, files in os.walk(RES):
    for f in sorted(files):
        p = os.path.join(root, f)
        print("  ", os.path.relpath(p, RES).replace("\\", "/"), os.path.getsize(p), "bytes")
