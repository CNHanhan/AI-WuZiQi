#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从 Wikimedia Commons 搜索并下载贴题的真实照片，替换商品预览图。
只保留位图(jpg/png/webp)，排除 PDF/SVG；带限流退避。"""
import os, json, time, urllib.request, urllib.parse
from PIL import Image

DST = r"C:\Users\Han\Documents\QT\AI-WuZiQi\client\resources\res"
UA = {"User-Agent": "AIWuZiQiLocalTest/1.0 (image fetch for local game project)"}

# 关键词 -> 目标相对路径（更贴题的搜索词）
QUERIES = {
    "board_classic":  ("chessboard wooden pieces", "board/classic.png"),
    "board_glass":    ("blue glass abstract texture", "board/glass.png"),
    "board_galaxy":   ("milky way galaxy night sky", "board/galaxy.png"),
    "stone_glass":    ("glass marble sphere", "stone/glass.png"),
    "stone_agate":    ("agate gemstone polished", "stone/agate.png"),
    "stone_jade":     ("jade gemstone green", "stone/jade.png"),
    "effect_glow":    ("neon light long exposure", "effect/glow.png"),
    "effect_firework":("fireworks display night", "effect/firework.png"),
    "avatar_default": ("smiling person portrait", "avatar/default.png"),
    "avatar_coffee":  ("coffee cup latte", "avatar/coldcoffee.png"),
    "avatar_fujiang": ("young woman portrait", "avatar/fujiang.png"),
}

def api_search(q):
    params = {
        "action": "query", "generator": "search", "gsrsearch": q + " filetype:bitmap",
        "gsrnamespace": "6", "gsrlimit": "8", "prop": "imageinfo",
        "iiprop": "url|mime", "iiurlwidth": "400", "format": "json",
    }
    url = "https://commons.wikimedia.org/w/api.php?" + urllib.parse.urlencode(params)
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=25) as r:
        data = json.loads(r.read().decode("utf-8"))
    pages = data.get("query", {}).get("pages", {})
    out = []
    for p in pages.values():
        ii = p.get("imageinfo", [{}])[0]
        mime = ii.get("mime", "")
        thumb = ii.get("thumburl", "")
        if thumb and mime in ("image/jpeg", "image/png", "image/webp"):
            low = thumb.lower()
            if low.endswith((".jpg", ".jpeg", ".png", ".webp")) or "page1-" not in low:
                out.append(thumb)
    return out

def download(url, path):
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=30) as r:
        content = r.read()
    with open(path, "wb") as f:
        f.write(content)

for key, (kw, rel) in QUERIES.items():
    done = False
    for attempt in range(3):
        try:
            urls = api_search(kw)
        except Exception as e:
            print("SEARCH FAIL", key, str(e)[:60]); time.sleep(2); continue
        for u in urls[:4]:
            try:
                tmp = os.path.join(os.path.dirname(DST), "_tmp_img.jpg")
                download(u, tmp)
                im = Image.open(tmp)
                if im.width < 100 or im.height < 100:
                    continue
                im = im.convert("RGB")
                out = os.path.join(DST, rel.replace("/", os.sep))
                os.makedirs(os.path.dirname(out), exist_ok=True)
                im.save(out, "PNG", optimize=True)
                print("OK", rel, os.path.getsize(out), "bytes <-", u.split("/")[-1][:40])
                done = True
                break
            except Exception as e:
                print("  dl fail", str(e)[:50]); continue
        if done: break
        time.sleep(1.5)
    if not done:
        print("KEEP OLD", rel, "(下载失败，保留现有图)")
    time.sleep(1.2)  # Wikimedia 限流退避

# 两个特效的 webp 同步更新为最新 png 内容
for k, rel in (("effect_glow", "effect/glow.png"), ("effect_firework", "effect/firework.png")):
    png = os.path.join(DST, rel.replace("/", os.sep))
    if os.path.exists(png):
        im = Image.open(png).convert("RGB")
        webp = os.path.join(DST, "effect", k.split("_",1)[1] + ".webp")
        im.save(webp, "WEBP", quality=85)
        print("WEBP", os.path.basename(webp), os.path.getsize(webp))
