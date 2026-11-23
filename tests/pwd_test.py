#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""验证简单密码 1124 能注册+登录（模拟爸爸的场景）"""
import json, struct, socket, ssl, time, itertools, hashlib, os, sys
HOST, PORT = "127.0.0.1", 8889
seq = itertools.count(1)
def nonce(): return hashlib.sha256(os.urandom(16)).hexdigest()[:32]
raw = socket.create_connection((HOST, PORT), timeout=10)
ctx = ssl.create_default_context(); ctx.check_hostname=False; ctx.verify_mode=ssl.CERT_NONE
s = ctx.wrap_socket(raw, server_hostname=HOST)
buf = b""
def rx(n):
    global buf
    while len(buf) < n:
        c = s.recv(65536)
        if not c: raise EOFError
        buf += c
    o, buf = buf[:n], buf[n:]
    return o
def req(t, d, expect):
    n = next(seq)
    m = {"type":t,"seq":n,"nonce":nonce(),"timestamp":int(time.time()),"data":d}
    p = json.dumps(m).encode(); s.sendall(struct.pack(">I", len(p)) + p)
    end = time.time()+6
    while time.time() < end:
        ln = struct.unpack(">I", rx(4))[0]
        msg = json.loads(rx(ln).decode())
        if msg.get("seq")==n and msg.get("type")==expect:
            return msg
    return None

user = "u1124_" + str(int(time.time()))[-5:]
r = req("register_req", {"username":user,"password":"1124","email":user+"@qq.com"}, "register_resp")
print("注册(密码1124):", "PASS" if r and r["data"]["code"]==0 else f"FAIL {r}")
r2 = req("login_req", {"username":user,"password":"1124"}, "login_resp")
print("登录(密码1124):", "PASS" if r2 and r2["data"]["code"]==0 else f"FAIL {r2}")
# 错误密码应被拒
r3 = req("login_req", {"username":user,"password":"9999"}, "login_resp")
print("错误密码被拒:", "PASS" if r3 and r3["data"]["code"]==1003 else f"FAIL {r3}")
s.close()
