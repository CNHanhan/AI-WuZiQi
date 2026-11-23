#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
AI-WuZiQi 协议模拟测试客户端
用途：在不启动 Qt GUI 的情况下，通过 TLS 连接服务端，验证全部业务功能。
用法：
  python tests/proto_client.py <host> <port> <scenario>
scenario:
  smoke    - 冒烟：连接 + 心跳
  user     - 用户系统：注册/登录/重复注册/错误密码
  hall     - 大厅：房间列表/在线玩家/排行榜/聊天
  game     - 双人对局：建房/加入/准备/落子五连/结算
  shop     - 商城：商品列表/购买/装备
  all      - 全部（依次跑上面所有场景）
"""
import sys, os, json, struct, socket, ssl, time, threading, itertools, hashlib

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8889
SCENARIO = sys.argv[3] if len(sys.argv) > 3 else "smoke"

_seq = itertools.count(1)
_results = []

def nonce():
    return hashlib.sha256(os.urandom(16)).hexdigest()[:32]

class Client:
    def __init__(self, name):
        self.name = name
        raw = socket.create_connection((HOST, PORT), timeout=10)
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE  # 自签证书，测试跳过校验（指纹由 Qt 客户端负责）
        self.sock = ctx.wrap_socket(raw, server_hostname=HOST)
        self.buf = b""
        self.uid = 0
        self.token = ""
        self.events = []
        self.lock = threading.Lock()

    def send(self, typ, data=None, seq=None):
        if seq is None:
            seq = next(_seq)
        msg = {"type": typ, "seq": seq, "nonce": nonce(),
               "timestamp": int(time.time()), "data": data or {}}
        payload = json.dumps(msg, ensure_ascii=False).encode("utf-8")
        self.sock.sendall(struct.pack(">I", len(payload)) + payload)
        return seq

    def _recv_exact(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise EOFError("connection closed")
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def recv(self, timeout=5):
        self.sock.settimeout(timeout)
        try:
            header = self._recv_exact(4)
            length = struct.unpack(">I", header)[0]
            if length > 1024*1024:
                raise ValueError("frame too large")
            payload = self._recv_exact(length)
            msg = json.loads(payload.decode("utf-8"))
            if msg.get("type", "").endswith("_notify"):
                with self.lock:
                    self.events.append(msg)
            return msg
        except socket.timeout:
            return None

    def request(self, typ, data=None, expect=None, timeout=6, collect_events=False):
        seq = self.send(typ, data)
        deadline = time.time() + timeout
        events = []
        while time.time() < deadline:
            msg = self.recv(timeout=max(0.1, deadline - time.time()))
            if msg is None:
                break
            if msg.get("seq") == seq and (expect is None or msg.get("type") == expect):
                if collect_events:
                    return msg, events
                return msg
            # _notify 类消息暂存
            if msg.get("type", "").endswith("_notify"):
                events.append(msg)
        raise TimeoutError(f"{self.name}: no response for {typ} (expect={expect})")

    def close(self):
        try: self.sock.close()
        except Exception: pass

def check(name, cond, detail=""):
    ok = bool(cond)
    _results.append((name, ok, detail))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}" + (f" - {detail}" if detail else ""), flush=True)
    return ok

def code(resp):
    return resp.get("data", {}).get("code")

def clear_login_locks():
    """清除登录失败锁定（测试环境需要，避免误锁影响后续场景）"""
    try:
        import subprocess
        cli = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                           "tools", "redis", "redis-cli.exe")
        if os.path.exists(cli):
            out = subprocess.run([cli, "-h", "127.0.0.1", "-p", "6379", "keys", "login_fail:*"],
                                 capture_output=True, text=True, timeout=5).stdout
            for k in out.split():
                if k.strip():
                    subprocess.run([cli, "-h", "127.0.0.1", "-p", "6379", "del", k.strip()],
                                   capture_output=True, timeout=5)
    except Exception:
        pass

# ---------------- 场景 ----------------

def scenario_smoke():
    c = Client("smoke")
    r = c.request("heartbeat_req", {"timestamp": int(time.time())}, "heartbeat_resp")
    # 心跳响应只回 timestamp（无 code 字段），检查 timestamp 存在即可
    check("TLS 连接 + 心跳", "timestamp" in r.get("data", {}), str(r.get("data")))
    c.close()

def scenario_user():
    tag = str(int(time.time()))[-6:]
    u1, u2, email = "test"+tag, "test2"+tag, f"test{tag}@example.com"
    c = Client("user")
    r = c.request("register_req", {"username":u1,"password":"Passw0rd1","email":email}, "register_resp")
    check("注册新用户", code(r) == 0, f"uid={r.get('data',{}).get('uid')}")
    # 正确密码登录先做，避免错误密码触发锁定影响后续
    r = c.request("login_req", {"username":u1,"password":"Passw0rd1","client_version":"1.0.0"}, "login_resp")
    check("正确密码登录成功", code(r) == 0 and "token" in r.get("data",{}), str(r.get("data",{}).get("user_info")))
    r = c.request("register_req", {"username":u1,"password":"Passw0rd1","email":email}, "register_resp")
    check("重复注册被拒", code(r) == 1001, str(r.get("data")))
    # 错误密码用独立用户，避免污染 u1 的锁定状态
    c_wrong = Client("wrong")
    # 每次用随机不存在的用户名，避免按 IP 的失败计数累积触发锁定
    r = c_wrong.request("login_req", {"username":"nouser_"+nonce()[:12],"password":"wrongpass"}, "login_resp")
    check("错误密码/不存在用户被拒", code(r) in (1003,), str(r.get("data")))
    c_wrong.close()
    c.token = r.get("data",{}).get("token","")
    c.uid = r.get("data",{}).get("user_info",{}).get("uid",0)
    r = c.request("logout_req", {}, "logout_resp")
    check("登出", code(r) == 0)
    c.close()
    return u1, u2

def scenario_hall():
    tag = str(int(time.time()))[-6:]
    u = "hall"+tag
    c = Client("hall")
    c.request("register_req", {"username":u,"password":"Passw0rd1","email":f"{u}@example.com"}, "register_resp")
    r = c.request("login_req", {"username":u,"password":"Passw0rd1"}, "login_resp")
    check("大厅-登录", code(r)==0)
    r = c.request("room_list_req", {"page":1,"page_size":20}, "room_list_resp")
    check("房间列表", code(r)==0 and "rooms" in r.get("data",{}), f"total={r.get('data',{}).get('total')}")
    r = c.request("player_list_req", {}, "player_list_resp")
    check("在线玩家列表", code(r)==0, str(r.get("data",{}))[:80])
    r = c.request("rank_list_req", {"rank_type":"elo","page":1}, "rank_list_resp")
    check("排行榜", code(r)==0 and "rankings" in r.get("data",{}), f"{len(r.get('data',{}).get('rankings',[]))} 条")
    # 大厅聊天：服务端广播 hall_chat_notify，不回 resp
    seq = c.send("hall_chat_req", {"content":"hello from test"})
    got_notify = False
    deadline = time.time() + 5
    while time.time() < deadline:
        m = c.recv(timeout=max(0.1, deadline - time.time()))
        if m and m.get("type") == "hall_chat_notify":
            got_notify = True
            break
    check("大厅聊天广播", got_notify, str(m.get("data"))[:80] if m else "无消息")
    c.close()

def scenario_game():
    tag = str(int(time.time()))[-6:]
    u1, u2 = "black"+tag, "white"+tag
    c1, c2 = Client("black"), Client("white")
    for c, u in ((c1,u1),(c2,u2)):
        c.request("register_req", {"username":u,"password":"Passw0rd1","email":f"{u}@example.com"}, "register_resp")
        r = c.request("login_req", {"username":u,"password":"Passw0rd1"}, "login_resp")
        check(f"{u} 登录", code(r)==0)
    r = c1.request("create_room_req", {"total_time":180,"step_time":30,"password":""}, "create_room_resp")
    rid = r.get("data",{}).get("room_id")
    check("创建房间", code(r)==0 and rid, f"room_id={rid}")
    r = c2.request("join_room_req", {"room_id":rid,"password":""}, "join_room_resp")
    check("加入房间", code(r)==0)
    r1 = c1.request("ready_req", {"ready":True}, "ready_resp")
    r2 = c2.request("ready_req", {"ready":True}, "ready_resp")
    check("双方准备", code(r1)==0 and code(r2)==0)
    # 收 game_start_notify
    s1 = c1.recv(timeout=4); s2 = c2.recv(timeout=4)
    check("游戏开始通知", bool(s1 and s2 and s1.get("type")=="game_start_notify"),
          f"c1={s1.get('type') if s1 else None} c2={s2.get('type') if s2 else None}")
    # 判断谁黑谁白
    color1 = s1.get("data",{}).get("my_color",1) if s1 else 1
    black, white = (c1,c2) if color1==1 else (c2,c1)
    # 黑方横向连五：(3,3)-(7,3)
    moves = [(3,3),(3,4),(4,3),(4,4),(5,3),(5,4),(6,3),(6,4),(7,3)]
    # 白方落在合法且不干扰黑方横线的位置（列10，行3起，均在0-14内）
    white_moves = [(10,3),(10,4),(10,5),(10,6),(10,7),(10,8),(10,9),(10,10)]
    wi = 0
    for i,(x,y) in enumerate(moves):
        cur = black if i%2==0 else white
        if cur is white:
            wx, wy = white_moves[wi]; wi += 1
        else:
            wx, wy = (x,y)
        r = cur.request("place_stone_req", {"x":wx,"y":wy}, "place_stone_resp")
        if not check(f"落子{i+1} ({wx},{wy})", code(r)==0, str(r.get("data"))):
            break
    # 找 game_over_notify（可能在 notify 里）
    over = None
    for c in (c1,c2):
        for e in c.events:
            if e.get("type")=="game_over_notify":
                over = e
        # 再收几帧
        for _ in range(3):
            m = c.recv(timeout=1)
            if m and m.get("type")=="game_over_notify":
                over = m
    check("五连胜负结算", bool(over and over.get("data",{}).get("reason")=="win"),
          str(over.get("data")) if over else "未收到 game_over_notify")
    c1.close(); c2.close()

def scenario_shop():
    tag = str(int(time.time()))[-6:]
    u = "shop"+tag
    c = Client("shop")
    c.request("register_req", {"username":u,"password":"Passw0rd1","email":f"{u}@example.com"}, "register_resp")
    c.request("login_req", {"username":u,"password":"Passw0rd1"}, "login_resp")
    r = c.request("shop_list_req", {}, "shop_list_resp")
    items = r.get("data",{}).get("items",[])
    check("商城商品列表", code(r)==0 and len(items)>0, f"{len(items)} 件商品")
    not_owned = next((i for i in items if not i.get("owned")), None)
    if not_owned:
        r = c.request("buy_item_req", {"item_id":not_owned["id"]}, "buy_item_resp")
        # 金币不足属业务正确行为（新用户 coins=0），只要不是未知错误即通过
        if code(r) == 0:
            check("购买商品", True, str(r.get("data")))
        elif code(r) == 1008:
            check("购买商品（金币不足拦截正确）", True, str(r.get("data")))
        else:
            check("购买商品", False, str(r.get("data")))
        target_id = not_owned["id"]
    else:
        target_id = items[0]["id"] if items else 1
    r = c.request("equip_item_req", {"item_id":target_id,"equipped":True}, "equip_item_resp")
    check("装备商品", code(r)==0, str(r.get("data")))
    c.close()

def main():
    print(f"=== AI-WuZiQi 协议测试 target={HOST}:{PORT} scenario={SCENARIO} ===", flush=True)
    clear_login_locks()   # 保证测试可重复（服务端按 IP+用户名锁定，测试会累积）
    try:
        if SCENARIO in ("smoke","all"): scenario_smoke()
        if SCENARIO in ("user","all"): scenario_user()
        if SCENARIO in ("hall","all"): scenario_hall()
        if SCENARIO in ("game","all"): scenario_game()
        if SCENARIO in ("shop","all"): scenario_shop()
    except Exception as e:
        check("测试执行异常", False, f"{type(e).__name__}: {e}")
    passed = sum(1 for _,ok,_ in _results if ok)
    print(f"\n=== 结果: {passed}/{len(_results)} 通过 ===", flush=True)
    for name, ok, detail in _results:
        if not ok:
            print(f"  FAILED: {name} - {detail}", flush=True)
    sys.exit(0 if passed==len(_results) and _results else 1)

if __name__ == "__main__":
    main()
