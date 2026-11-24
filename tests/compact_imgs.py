#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把商品预览图统一压缩到 400x300，控制 exe 体积；保持 PNG/WEBP 格式"""
import os
from PIL import Image

DST = r"C:\Users\Han\Documents\QT\AI-WuZiQi\client\resources\res"
MAX = (400, 300)

total_before = total_after = 0
for root, _, files in os.walk(DST):
    for fn in files:
        path = os.path.join(root, fn)
        before = os.path.getsize(path)
        total_before += before
        im = Image.open(path)
        fmt = "WEBP" if fn.lower().endswith(".webp") else "PNG"
        im.thumbnail(MAX, Image.LANCZOS)
        if fmt == "WEBP":
            im.save(path, "WEBP", quality=82)
        else:
            im.convert("RGB").save(path, "PNG", optimize=True)
        after = os.path.getsize(path)
        total_after += after
        print(f"{os.path.relpath(path, DST):28} {before:>7} -> {after:>7} bytes  {Image.open(path).format}")
print(f"\nTOTAL {total_before} -> {total_after} bytes (省 {total_before-total_after})")
