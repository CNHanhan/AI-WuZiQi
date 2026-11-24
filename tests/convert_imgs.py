#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把从网上下载的真实图片转成 PNG 并替换项目里的商品预览图"""
import os
from PIL import Image

TMP = r"C:\Users\Han\Documents\QT\AI-WuZiQi\tests\dl_imgs"
DST = r"C:\Users\Han\Documents\QT\AI-WuZiQi\client\resources\res"

MAP = {
    "board_classic": "board/classic.png",
    "board_glass": "board/glass.png",
    "board_galaxy": "board/galaxy.png",
    "stone_glass": "stone/glass.png",
    "stone_agate": "stone/agate.png",
    "stone_jade": "stone/jade.png",
    "effect_glow": "effect/glow.png",
    "effect_firework": "effect/firework.png",
    "avatar_default": "avatar/default.png",
    "avatar_coffee": "avatar/coldcoffee.png",
    "avatar_fujiang": "avatar/fujiang.png",
}

for key, rel in MAP.items():
    src = os.path.join(TMP, key + ".jpg")
    if not os.path.exists(src):
        print("SKIP missing", key)
        continue
    im = Image.open(src).convert("RGB")
    out = os.path.join(DST, rel.replace("/", os.sep))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    im.save(out, "PNG", optimize=True)
    print("SAVED", rel, os.path.getsize(out), "bytes")

# 两张特效的 .webp 也用同图更新（DB 里 resource_path 是 .webp）
for key in ("effect_glow", "effect_firework"):
    src = os.path.join(TMP, key + ".jpg")
    if os.path.exists(src):
        im = Image.open(src).convert("RGB")
        out = os.path.join(DST, "effect", key.split("_", 1)[1] + ".webp")
        im.save(out, "WEBP", quality=85)
        print("SAVED", os.path.basename(out), os.path.getsize(out), "bytes")
