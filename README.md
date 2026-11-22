# AI-WuZiQi — 五子棋对战平台

基于 C/S 架构的网络五子棋对战平台，支持**联网对战**与**离线 AI 对战**双模式。

## 技术栈

| 组件 | 技术 |
|------|------|
| 服务端 | C++11 + Linux + epoll Reactor + MySQL 8.0 + Redis |
| 客户端 | Qt 5.15 / 6.5 Widgets + QPainter + QSslSocket |
| 通信 | JSON over TCP，帧 `[4B大端长度][Payload]`，TLS 1.3 + AES-256-GCM |
| AI 引擎 | 贪心威胁检测 / Alpha-Beta 剪枝 / MCTS 蒙特卡洛树搜索 |
| 加密 | OpenSSL：TLS1.3 / AES-GCM / ECDH / ECDSA P-256 / Argon2id / HMAC-SHA256 |

## 目录结构

```
AI-WuZiQi/
├── 00~06-*.md       # 设计文档（概述/服务端/客户端/协议/数据库/AI/加密）
├── common/          # 两端共用：协议编解码、棋盘、校验、加密、AI 引擎
├── proto/           # 协议定义
├── sql/             # 建表与初始化脚本
├── server/          # 服务端（net / db / ai / src）
├── client/          # Qt 客户端（ui / src / resources / crypto）
└── tests/           # 单元测试
```

## 快速开始

### 服务端（Linux）
```bash
cd server && mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release .. && make -j$(nproc)
./AIWZQ_Server
```

### 客户端（Windows）
```bash
qmake client/AI-WuZiQi.pro && mingw32-make
```

## 功能模块

- 用户系统：注册/登录/找回密码/修改密码、Token 会话、Argon2id 密码哈希、失败锁定
- 游戏大厅：房间列表、在线玩家、公共聊天、ELO 排行榜
- 房间对局：创建/加入/随机匹配、准备/悔棋/和棋/认输、3 分钟总时限 + 30 秒步时限
- 离线 AI：无需登录，三档难度
- 商城：棋盘/棋子/特效/头像皮肤，金币体系
- 安全：TLS 1.3、证书固定、nonce+timestamp 防重放、速率限制、安全审计日志
