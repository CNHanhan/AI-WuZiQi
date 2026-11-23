#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""验证商城购买成功路径：用已有金币的用户买一件未拥有的商品"""
import sys, json, struct, socket, ssl, time, itertools, hashlib, os
HOST, PORT = "127.0.0.1", 8889
USER = sys.argv[1] if len(sys.argv)>1 else "shop460565"
PWD = "Passw0rd1"
seq = itertools.count(1)
def nonce(): return hashlib.sha256(os.urandom(16)).hexdigest()[:32]

raw = socket.create_connection((HOST,PORT),timeout=10)
ctx = ssl.create_default_context(); ctx.check_hostname=False; ctx.verify_mode=ssl.CERT_NONE
s = ctx.wrap_socket(raw, server_hostname=HOST)
buf=b""
def recv_exact(n):
    global buf
    while len(buf)<n:
        c=s.recv(65536)
        if not c: raise EOFError
        buf+=c
    o,buf=buf[:n],buf[n:]; return o
def send(t,d):
    n=next(seq); m={"type":t,"seq":n,"nonce":nonce(),"timestamp":int(time.time()),"data":d}
    p=json.dumps(m).encode(); s.sendall(struct.pack(">I",len(p))+p); return n
def req(t,d,expect,timeout=6):
    n=send(t,d); end=time.time()+timeout
    while time.time()<end:
        h=recv_exact(4); ln=struct.unpack(">I",h)[0]; msg=json.loads(recv_exact(ln).decode())
        if msg.get("seq")==n and msg.get("type")==expect: return msg
    return None

login=req("login_req",{"username":USER,"password":PWD},"login_resp")
print("登录:", "PASS" if login and login["data"]["code"]==0 else f"FAIL {login}")
lst=req("shop_list_req",{},"shop_list_resp")
items=lst["data"]["items"]
print(f"商品数: {len(items)}")
not_owned=[i for i in items if not i["owned"]]
print("未拥有商品:", [(i["id"],i["name"],i["price"]) for i in not_owned[:3]])
if not_owned:
    target=not_owned[0]
    buy=req("buy_item_req",{"item_id":target["id"]},"buy_item_resp")
    ok = buy and buy["data"]["code"]==0
    print("购买成功路径:", "PASS" if ok else f"FAIL {buy}")
    if ok:
        req("equip_item_req",{"item_id":target["id"],"equipped":True},"equip_item_resp")
        print("装备已购商品: PASS")
s.close()
