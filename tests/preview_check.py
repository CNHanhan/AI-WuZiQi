#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""验证 shop_list_resp 下发 preview_image 字段"""
import json, struct, socket, ssl, time, itertools, hashlib, os
HOST, PORT = "127.0.0.1", 8889
seq = itertools.count(1)
def nonce(): return hashlib.sha256(os.urandom(16)).hexdigest()[:32]
raw = socket.create_connection((HOST,PORT), timeout=10)
ctx = ssl.create_default_context(); ctx.check_hostname=False; ctx.verify_mode=ssl.CERT_NONE
s = ctx.wrap_socket(raw, server_hostname=HOST)
buf=b""
def rx(n):
    global buf
    while len(buf)<n:
        c=s.recv(65536)
        if not c: raise EOFError
        buf+=c
    o,buf=buf[:n],buf[n:]; return o
def req(t,d,expect):
    n=next(seq); m={"type":t,"seq":n,"nonce":nonce(),"timestamp":int(time.time()),"data":d}
    p=json.dumps(m).encode(); s.sendall(struct.pack(">I",len(p))+p)
    end=time.time()+6
    while time.time()<end:
        ln=struct.unpack(">I",rx(4))[0]
        msg=json.loads(rx(ln).decode())
        if msg.get("seq")==n and msg.get("type")==expect: return msg
    return None
u="img"+hashlib.md5(os.urandom(4)).hexdigest()[:6]
req("register_req",{"username":u,"password":"1124","email":u+"@q.com"},"register_resp")
req("login_req",{"username":u,"password":"1124"},"login_resp")
r=req("shop_list_req",{},"shop_list_resp")
items=r["data"]["items"]
print("商品数:",len(items))
all_have=all("preview_image" in it and it["preview_image"] for it in items)
for it in items[:4]: print(" ", it["name"], "->", it.get("preview_image","(无字段)"))
print("[PASS] 全部下发预览图路径" if all_have else "[FAIL] 缺预览图路径")
s.close()
