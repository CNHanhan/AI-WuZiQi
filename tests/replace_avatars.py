#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""用爸爸提供的头像图替换商城头像商品图（去掉惊悚人像）"""
import os
from PIL import Image

SRC = r"C:\Users\Han\Documents\QT\AI-WuZiQi\头像"
DST = r"C:\Users\Han\Documents\QT\AI-WuZiQi\client\resources\res\avatar"

# 商品 -> 爸爸的图
MAP = {
    "default.png": "images.jpg",       # 默认头像 -> 酷猫墨镜
    "coldcoffee.png": "images (1).jpg",  # 冷咖啡 -> 红狐狸呲牙
    "fujiang.png": "images (3).jpg",   # 富江 -> 动漫少女（替换惊悚人像）
}

for dst_name, src_name in MAP.items():
    src = os.path.join(SRC, src_name)
    im = Image.open(src).convert("RGB")
    im.thumbnail((400, 400), Image.LANCZOS)
    out = os.path.join(DST, dst_name)
    im.save(out, "PNG", optimize=True)
    print("SAVED", dst_name, "<-", src_name, im.size, os.path.getsize(out), "bytes")
