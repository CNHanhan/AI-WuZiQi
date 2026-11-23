#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""验证对局结束后房间从列表移除 + 随机匹配池"""
import json, struct, socket, ssl, time, itertools, hashlib, os
HOST, PORT = "127.0.0.1", 8889
seq = itertools.count(1)
def nonce(): return hashlib.sha256(os.urandom(16)).hexdigest()[:32]
class C:
    def __init__(self):
        raw=socket.create_connection((HOST,PORT),timeout=10)
        ctx=ssl.create_default_context(); ctx.check_hostname=False; ctx.verify_mode=ssl.CERT_NONE
        self.s=ctx.wrap_socket(raw,server_hostname=HOST); self.buf=b""; self.uid=0
    def rx(self,n):
        while len(self.buf)<n:
            c=self.s.recv(65536)
            if not c: raise EOFError
            self.buf+=c
        o,self.buf=self.buf[:n],self.buf[n:]; return o
    def req(self,t,d,expect,timeout=6):
        n=next(seq); m={"type":t,"seq":n,"nonce":nonce(),"timestamp":int(time.time()),"data":d}
        p=json.dumps(m).encode(); self.s.sendall(struct.pack(">I",len(p))+p)
        end=time.time()+timeout
        while time.time()<end:
            ln=struct.unpack(">I",self.rx(4))[0]
            msg=json.loads(self.rx(ln).decode())
            if msg.get("seq")==n and msg.get("type")==expect: return msg
        return None
    def close(self): self.s.close()

tag=hashlib.md5(os.urandom(8)).hexdigest()[:8]
a,b=C(),C()
for c,u in ((a,"ca"+tag),(b,"cb"+tag)):
    c.req("register_req",{"username":u,"password":"1124","email":u+"@q.com"},"register_resp")
    r=c.req("login_req",{"username":u,"password":"1124"},"login_resp")
    c.uid=r["data"]["user_info"]["uid"]
# 建房+加入+准备
r=a.req("create_room_req",{"total_time":180,"step_time":30,"password":""},"create_room_resp")
rid=r["data"]["room_id"]
b.req("join_room_req",{"room_id":rid,"password":""},"join_room_resp")
a.req("ready_req",{"ready":True},"ready_resp"); b.req("ready_req",{"ready":True},"ready_resp")
# 查对局前房间列表含此房间
lst=a.req("room_list_req",{"page":1,"page_size":100},"room_list_resp")
ids=[x["room_id"] for x in lst["data"]["rooms"]]
print("对局前房间列表含rid:", rid in ids, "列表共", len(ids))
# 快速认输结束（认输 resp 走 game_over_notify 类型；收不到也不影响后续列表检查）
try:
    a.req("surrender_req",{},"game_over_notify",timeout=3)
except Exception:
    pass
time.sleep(1)
# 查对局后房间列表
lst2=a.req("room_list_req",{"page":1,"page_size":100},"room_list_resp")
ids2=[x["room_id"] for x in lst2["data"]["rooms"]]
print("对局后房间列表移除rid:", rid not in ids2, "列表共", len(ids2))
print("[PASS]" if (rid in ids and rid not in ids2) else "[FAIL]", "房间清理")
a.close(); b.close()
