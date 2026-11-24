/**
 * BizHandler.cpp — 业务消息分派与处理
 * 对应设计文档：01-服务端设计.md 2.3~2.8；03-通信协议设计.md 4.1~4.4/五；
 *               06-加密系统设计.md 2.4/3.2；common/include/IMessageHandler.h
 *
 * 处理全部 *_req 消息并回 *_resp / *_notify。对外的唯一契约 createBizHandler() 在文件末尾。
 */
#include "BizHandler.h"
#include "Config.h"
#include "Crypto.h"
#include "RateLimiter.h"
#include "Validator.h"

#include <algorithm>
#include <chrono>
#include <ctime>

namespace AIWZQ {

namespace {

int64_t nowSec() {
    return static_cast<int64_t>(std::time(nullptr));
}

// 从 json 安全取字符串/整数
std::string jstr(const json& j, const char* key, const std::string& def = "") {
    if (!j.is_object() || !j.contains(key)) return def;
    try { return j.at(key).get<std::string>(); } catch (...) { return def; }
}
int jint(const json& j, const char* key, int def = 0) {
    if (!j.is_object() || !j.contains(key)) return def;
    try { return j.at(key).get<int>(); } catch (...) { return def; }
}
bool jbool(const json& j, const char* key, bool def = false) {
    if (!j.is_object() || !j.contains(key)) return def;
    try { return j.at(key).get<bool>(); } catch (...) { return def; }
}

UserInfo toUserInfo(const User& u) {
    UserInfo info;
    info.uid = u.uid; info.username = u.username; info.eloScore = u.eloScore;
    info.wins = u.wins; info.losses = u.losses; info.draws = u.draws;
    info.coins = u.coins; info.avatar = u.avatar;
    return info;
}

json userInfoJson(const UserInfo& u) {
    return {
        {"uid", u.uid}, {"username", u.username}, {"elo_score", u.eloScore},
        {"wins", u.wins}, {"losses", u.losses}, {"draws", u.draws},
        {"coins", u.coins}, {"avatar", u.avatar}
    };
}

} // namespace

BizHandler::BizHandler()
    : sessions_(redis_), limiter_(redis_, sessions_),
      security_(db_), rooms_(db_, redis_) {
    // 首次启动连接 DB/Redis（失败时各模块方法会返回 false，服务继续运行）
    db_.connect();
    redis_.connect();
    rooms_.rebuildRankCache();

    // 后台每秒扫描超时房间（幂等，net 层 setOnTimer 也会调用 tick()）
    tickThread_ = std::thread([this]() {
        while (!tickStop_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (tickStop_.load()) break;
            tick();
        }
    });
}

BizHandler::~BizHandler() {
    tickStop_.store(true);
    if (tickThread_.joinable()) tickThread_.join();
    redis_.close();
    db_.close();
}

// ---------------- 通用工具 ----------------

void BizHandler::reply(IConnection* c, const Message& m,
                       const std::string& type, const json& data) {
    if (!c) return;
    Message out = makeMessage(type, m.seq, data, m.nonce, nowSec());
    c->send(out);
}

void BizHandler::replyError(IConnection* c, const Message& m,
                            const std::string& type, int code, const std::string& message) {
    reply(c, m, type, {{"code", code}, {"message", message}});
}

bool BizHandler::requireAuth(IConnection* c, const Message& m,
                             const std::string& respType, int64_t& uid) {
    if (!c) return false;
    if (c->uid() > 0) { uid = c->uid(); return true; }
    // 用连接状态里保存的 token 校验
    std::string token;
    {
        std::lock_guard<std::mutex> lk(connMtx_);
        auto it = connStates_.find(c ? c->id() : -1);
        if (it == connStates_.end() || it->second.token.empty()) {
            replyError(c, m, respType, ErrCode::TOKEN_INVALID, "token invalid");
            return false;
        }
        token = it->second.token;
    }
    if (!sessions_.validateSession(token, uid)) {
        replyError(c, m, respType, ErrCode::TOKEN_INVALID, "token expired");
        return false;
    }
    return true;
}

void BizHandler::broadcastRoom(int64_t roomId, const std::string& type, const json& data) {
    std::vector<IConnection*> targets;
    {
        std::lock_guard<std::mutex> lk(uidMtx_);
        // 房间双方 uid 从 Redis hash 取
        std::map<std::string, std::string> h;
        redis_.hgetall(redis_.roomKey(roomId), h);
        auto collect = [&](const char* k) {
            auto it = h.find(k);
            if (it == h.end()) return;
            int64_t uid = std::stoll(it->second);
            auto cit = uidConns_.find(uid);
            if (cit != uidConns_.end() && cit->second) targets.push_back(cit->second);
        };
        collect("black_uid"); collect("white_uid");
    }
    for (auto* c : targets) c->send(makeMessage(type, 0, data, "", nowSec()));
}

// ---------------- 连接事件 ----------------

void BizHandler::onConnected(IConnection* conn) {
    if (!conn) return;
    std::lock_guard<std::mutex> lk(hallMtx_);
    hallConns_.push_back(conn);
}

void BizHandler::onDisconnected(IConnection* conn) {
    if (!conn) return;
    {
        std::lock_guard<std::mutex> lk(hallMtx_);
        hallConns_.erase(std::remove(hallConns_.begin(), hallConns_.end(), conn), hallConns_.end());
    }
    int64_t uid = conn->uid();
    if (uid > 0) {
        redis_.del(redis_.onlineKey(uid));
        std::lock_guard<std::mutex> lk(uidMtx_);
        uidConns_.erase(uid);
        rooms_.leaveRoom(uid);
    }
    std::lock_guard<std::mutex> lk(connMtx_);
    connStates_.erase(conn->id());
}

// ---------------- 消息分派 ----------------

void BizHandler::onMessage(IConnection* conn, const Message& msg) {
    if (!conn) return;
    // API 频率限制
    if (!limiter_.checkApiRate(conn->peerIp())) {
        replyError(conn, msg, msg.type + "_resp", ErrCode::RATE_LIMITED, "请求过于频繁");
        return;
    }
    const std::string& t = msg.type;
    // 用户
    if      (t == MsgType::REGISTER_REQ)   handleRegister(conn, msg);
    else if (t == MsgType::LOGIN_REQ)      handleLogin(conn, msg);
    else if (t == MsgType::LOGOUT_REQ)     handleLogout(conn, msg);
    else if (t == MsgType::FIND_PWD_REQ)   handleFindPwd(conn, msg);
    else if (t == MsgType::CHANGE_PWD_REQ) handleChangePwd(conn, msg);
    // 大厅
    else if (t == MsgType::ROOM_LIST_REQ)   handleRoomList(conn, msg);
    else if (t == MsgType::PLAYER_LIST_REQ) handlePlayerList(conn, msg);
    else if (t == MsgType::HALL_CHAT_REQ)   handleHallChat(conn, msg);
    else if (t == MsgType::RANK_LIST_REQ)   handleRankList(conn, msg);
    // 房间
    else if (t == MsgType::CREATE_ROOM_REQ) handleCreateRoom(conn, msg);
    else if (t == MsgType::JOIN_ROOM_REQ)   handleJoinRoom(conn, msg);
    else if (t == MsgType::RANDOM_JOIN_REQ) handleRandomJoin(conn, msg);
    else if (t == MsgType::READY_REQ)       handleReady(conn, msg);
    // 对局
    else if (t == MsgType::PLACE_STONE_REQ) handlePlaceStone(conn, msg);
    else if (t == MsgType::UNDO_REQ)        handleUndo(conn, msg);
    else if (t == MsgType::DRAW_REQ)        handleDraw(conn, msg);
    else if (t == MsgType::SURRENDER_REQ)   handleSurrender(conn, msg);
    // 商城
    else if (t == MsgType::SHOP_LIST_REQ)   handleShopList(conn, msg);
    else if (t == MsgType::BUY_ITEM_REQ)    handleBuyItem(conn, msg);
    else if (t == MsgType::EQUIP_ITEM_REQ)  handleEquipItem(conn, msg);
    // 心跳/重连
    else if (t == MsgType::HEARTBEAT_REQ)   handleHeartbeat(conn, msg);
    else if (t == MsgType::RECONNECT_REQ)   handleReconnect(conn, msg);
    else replyError(conn, msg, t + "_resp", ErrCode::INVALID_PARAM, "unknown message type");
}

// ---------------- 用户系统 ----------------

void BizHandler::handleRegister(IConnection* c, const Message& m) {
    const std::string username = jstr(m.data, "username");
    const std::string password = jstr(m.data, "password");
    const std::string email    = jstr(m.data, "email");

    if (!Validator::validateUsername(username) ||
        !Validator::validatePassword(password) ||
        !Validator::validateEmail(email)) {
        replyError(c, m, MsgType::REGISTER_RESP, ErrCode::INVALID_PARAM, "输入参数无效");
        security_.logSuspiciousActivity(c->peerIp(), "register_invalid_input");
        return;
    }
    if (db_.queryUserByUsername(username)) {
        replyError(c, m, MsgType::REGISTER_RESP, ErrCode::USER_EXISTS, "用户名已存在");
        return;
    }
    User u;
    u.username = username;
    u.passwordHash = Crypto::hashPassword(password);   // Argon2id PHC
    u.salt = Crypto::randomHex(16);
    u.email = email;
    u.eloScore = 1500;
    u.coins = 0;
    if (!db_.insertUser(u)) {
        replyError(c, m, MsgType::REGISTER_RESP, ErrCode::INVALID_PARAM, "注册失败");
        return;
    }
    auto saved = db_.queryUserByUsername(username);
    reply(c, m, MsgType::REGISTER_RESP,
          {{"code", ErrCode::OK}, {"message", "注册成功"},
           {"uid", saved ? saved->uid : 0}});
    security_.logSensitiveOperation(saved ? saved->uid : 0, "register", "username=" + username);
}

void BizHandler::handleLogin(IConnection* c, const Message& m) {
    const std::string username = jstr(m.data, "username");
    const std::string password = jstr(m.data, "password");

    if (!limiter_.checkLoginAttempt(c->peerIp(), username)) {
        replyError(c, m, MsgType::LOGIN_RESP, ErrCode::ACCOUNT_LOCKED, "账户已被锁定，请稍后再试");
        security_.logLogin(c->peerIp(), username, false, "locked");
        return;
    }
    auto u = db_.queryUserByUsername(username);
    if (!u || u->status != 1 || !Crypto::verifyPassword(password, u->passwordHash)) {
        limiter_.checkLoginAttempt(c->peerIp(), username); // 累计失败
        replyError(c, m, MsgType::LOGIN_RESP, ErrCode::BAD_CREDENTIALS, "用户名或密码错误");
        security_.logLogin(c->peerIp(), username, false, "bad_credentials");
        return;
    }
    // 成功
    limiter_.clearLoginAttempts(c->peerIp(), username);
    const std::string token = sessions_.createSession(u->uid, c->peerIp());
    c->setUid(u->uid);
    {
        std::lock_guard<std::mutex> lk(connMtx_);
        connStates_[c->id()] = {u->uid, token};
    }
    {
        std::lock_guard<std::mutex> lk(uidMtx_);
        uidConns_[u->uid] = c;
    }
    UserInfo info = toUserInfo(*u);
    reply(c, m, MsgType::LOGIN_RESP,
          {{"code", ErrCode::OK}, {"message", "登录成功"},
           {"token", token}, {"user_info", userInfoJson(info)}});
    security_.logLogin(c->peerIp(), username, true, "ok");
}

void BizHandler::handleLogout(IConnection* c, const Message& m) {
    int64_t uid = 0;
    std::string token;
    {
        std::lock_guard<std::mutex> lk(connMtx_);
        auto it = connStates_.find(c->id());
        if (it != connStates_.end()) token = it->second.token;
    }
    if (!token.empty()) sessions_.destroySession(token);
    {
        std::lock_guard<std::mutex> lk(connMtx_);
        connStates_.erase(c->id());
    }
    {
        std::lock_guard<std::mutex> lk(uidMtx_);
        uidConns_.erase(c->uid());
        rooms_.leaveRoom(c->uid());
    }
    c->setUid(0);
    reply(c, m, MsgType::LOGOUT_RESP, {{"code", ErrCode::OK}, {"message", "已登出"}});
}

void BizHandler::handleFindPwd(IConnection* c, const Message& m) {
    // 阶段1：提交邮箱 -> 生成重置 token 存 Redis（30 分钟）
    // 阶段2：提交 token + new_password -> 更新密码
    const std::string email = jstr(m.data, "email");
    const std::string token = jstr(m.data, "token");
    const std::string newPwd = jstr(m.data, "new_password");

    if (token.empty()) {
        auto u = db_.queryUserByUsername(jstr(m.data, "username"));
        if (!u || u->email != email) {
            replyError(c, m, MsgType::FIND_PWD_RESP, ErrCode::BAD_CREDENTIALS, "邮箱与用户名不匹配");
            return;
        }
        const std::string resetToken = Crypto::randomHex(32);
        redis_.set("pwd_reset:" + resetToken, std::to_string(u->uid), PWD_RESET_TTL);
        // 生产环境此处应发邮件；本实现把 token 回给客户端用于闭环测试
        reply(c, m, MsgType::FIND_PWD_RESP,
              {{"code", ErrCode::OK}, {"message", "重置令牌已生成"}, {"token", resetToken}});
        security_.logSensitiveOperation(u->uid, "pwd_reset_request", "email=" + email);
        return;
    }
    std::string uidStr;
    if (!redis_.get("pwd_reset:" + token, uidStr) || !Validator::validatePassword(newPwd)) {
        replyError(c, m, MsgType::FIND_PWD_RESP, ErrCode::VERIFY_CODE_WRONG, "令牌无效或密码格式错误");
        return;
    }
    const int64_t uid = std::stoll(uidStr);
    db_.updatePassword(uid, Crypto::hashPassword(newPwd));
    redis_.del("pwd_reset:" + token);
    reply(c, m, MsgType::FIND_PWD_RESP, {{"code", ErrCode::OK}, {"message", "密码已重置"}});
    security_.logSensitiveOperation(uid, "pwd_reset_done", "token used");
}

void BizHandler::handleChangePwd(IConnection* c, const Message& m) {
    int64_t uid = 0;
    if (!requireAuth(c, m, MsgType::CHANGE_PWD_RESP, uid)) return;
    const std::string oldPwd = jstr(m.data, "old_password");
    const std::string newPwd = jstr(m.data, "new_password");
    if (!Validator::validatePassword(newPwd)) {
        replyError(c, m, MsgType::CHANGE_PWD_RESP, ErrCode::INVALID_PARAM, "新密码格式无效");
        return;
    }
    auto u = db_.queryUserByUid(uid);
    if (!u || !Crypto::verifyPassword(oldPwd, u->passwordHash)) {
        replyError(c, m, MsgType::CHANGE_PWD_RESP, ErrCode::BAD_CREDENTIALS, "原密码错误");
        return;
    }
    db_.updatePassword(uid, Crypto::hashPassword(newPwd));
    reply(c, m, MsgType::CHANGE_PWD_RESP, {{"code", ErrCode::OK}, {"message", "密码已修改"}});
    security_.logSensitiveOperation(uid, "password_change", "ok");
}

// ---------------- 大厅 ----------------

void BizHandler::handleRoomList(IConnection* c, const Message& m) {
    const int page = jint(m.data, "page", 1);
    const int size = jint(m.data, "page_size", 20);
    json rooms; int total = 0;
    rooms_.listRooms(page, size, rooms, total);
    reply(c, m, MsgType::ROOM_LIST_RESP,
          {{"code", ErrCode::OK}, {"total", total}, {"rooms", rooms}});
}

void BizHandler::handlePlayerList(IConnection* c, const Message& m) {
    json players = json::array();
    std::vector<int64_t> uids;
    {
        std::lock_guard<std::mutex> lk(uidMtx_);
        for (const auto& kv : uidConns_) uids.push_back(kv.first);
    }
    for (int64_t uid : uids) {
        UserInfo info;
        if (rooms_.userInfo(uid, info))
            players.push_back({{"uid", info.uid}, {"username", info.username},
                               {"elo_score", info.eloScore}, {"avatar", info.avatar}});
    }
    reply(c, m, MsgType::PLAYER_LIST_RESP, {{"code", ErrCode::OK}, {"players", players}});
}

void BizHandler::handleHallChat(IConnection* c, const Message& m) {
    int64_t uid = 0;
    if (!requireAuth(c, m, MsgType::HALL_CHAT_NOTIFY, uid)) return;
    if (!limiter_.checkChatRate(uid)) {
        replyError(c, m, MsgType::HALL_CHAT_REQ, ErrCode::RATE_LIMITED, "聊天过于频繁");
        return;
    }
    const std::string content = jstr(m.data, "content");
    if (!Validator::validateChatMessage(content)) {
        replyError(c, m, MsgType::HALL_CHAT_REQ, ErrCode::INVALID_PARAM, "聊天内容无效");
        security_.logSuspiciousActivity(uid, c->peerIp(), "invalid_chat");
        return;
    }
    UserInfo info;
    rooms_.userInfo(uid, info);
    json notify = {{"sender", info.username}, {"content", content}, {"timestamp", nowSec()}};
    std::vector<IConnection*> snapshot;
    {
        std::lock_guard<std::mutex> lk(hallMtx_);
        snapshot = hallConns_;
    }
    for (auto* target : snapshot)
        target->send(makeMessage(MsgType::HALL_CHAT_NOTIFY, 0, notify, "", nowSec()));
}

void BizHandler::handleRankList(IConnection* c, const Message& m) {
    const std::string rankType = jstr(m.data, "rank_type", "elo");
    const int page = jint(m.data, "page", 1);
    json rankings;
    rooms_.rankList(rankType, page, 20, rankings);
    reply(c, m, MsgType::RANK_LIST_RESP, {{"code", ErrCode::OK}, {"rankings", rankings}});
}

// ---------------- 房间 ----------------

void BizHandler::handleCreateRoom(IConnection* c, const Message& m) {
    int64_t uid = 0;
    if (!requireAuth(c, m, MsgType::CREATE_ROOM_RESP, uid)) return;
    const int total = jint(m.data, "total_time", 180);
    const int step = jint(m.data, "step_time", 30);
    const std::string pwd = jstr(m.data, "password");
    const int64_t roomId = rooms_.createRoom(uid, total, step, pwd);
    reply(c, m, MsgType::CREATE_ROOM_RESP,
          {{"code", ErrCode::OK}, {"room_id", roomId}});
}

void BizHandler::handleJoinRoom(IConnection* c, const Message& m) {
    int64_t uid = 0;
    if (!requireAuth(c, m, MsgType::JOIN_ROOM_RESP, uid)) return;
    const int64_t roomId = jint(m.data, "room_id", 0);
    const std::string pwd = jstr(m.data, "password");
    const int code = rooms_.joinRoom(roomId, uid, pwd);
    if (code != ErrCode::OK) {
        replyError(c, m, MsgType::JOIN_ROOM_RESP, code, "加入房间失败");
        return;
    }
    {
        std::lock_guard<std::mutex> lk(uidMtx_);
        uidConns_[uid] = c;
    }
    reply(c, m, MsgType::JOIN_ROOM_RESP, {{"code", ErrCode::OK}, {"room_id", roomId}});
}

void BizHandler::handleRandomJoin(IConnection* c, const Message& m) {
    int64_t uid = 0;
    if (!requireAuth(c, m, MsgType::ROOM_INFO_RESP, uid)) return;
    const int64_t roomId = rooms_.randomMatch(uid);
    {
        std::lock_guard<std::mutex> lk(uidMtx_);
        uidConns_[uid] = c;
    }
    reply(c, m, MsgType::ROOM_INFO_RESP, {{"code", ErrCode::OK}, {"room_id", roomId}});
}

void BizHandler::sendGameStart_(IConnection* c, int64_t uid, int64_t roomId) {
    std::map<std::string, std::string> h;
    redis_.hgetall(redis_.roomKey(roomId), h);
    auto getLL = [&](const char* k) -> int64_t {
        auto it = h.find(k);
        return it == h.end() ? 0 : std::stoll(it->second);
    };
    const int64_t black = getLL("black_uid"), white = getLL("white_uid");
    const int myColor = (uid == black) ? 1 : 2;
    const int64_t oppUid = (uid == black) ? white : black;
    UserInfo opp; rooms_.userInfo(oppUid, opp);
    json data = {{"my_color", myColor},
                 {"opponent_name", opp.username},
                 {"opponent_avatar", opp.avatar},
                 {"total_time_black", Config::instance().game().totalTimeSec},
                 {"total_time_white", Config::instance().game().totalTimeSec}};
    c->send(makeMessage(MsgType::GAME_START_NOTIFY, 0, data, "", nowSec()));
}

void BizHandler::handleReady(IConnection* c, const Message& m) {
    int64_t uid = 0;
    if (!requireAuth(c, m, MsgType::READY_RESP, uid)) return;
    GameOverEvent startEv;
    const int code = rooms_.ready(uid, jbool(m.data, "ready", true), &startEv);
    if (code != ErrCode::OK) {
        replyError(c, m, MsgType::READY_RESP, code, "准备失败");
        return;
    }
    reply(c, m, MsgType::READY_RESP, {{"code", ErrCode::OK}});
    if (startEv.reasonText == "start") {
        // 向双方发送 game_start_notify
        auto sendTo = [&](int64_t targetUid) {
            std::lock_guard<std::mutex> lk(uidMtx_);
            auto it = uidConns_.find(targetUid);
            if (it != uidConns_.end() && it->second)
                sendGameStart_(it->second, targetUid, startEv.roomId);
        };
        sendTo(startEv.blackUid);
        sendTo(startEv.whiteUid);
    }
}

// ---------------- 对局 ----------------

void BizHandler::handlePlaceStone(IConnection* c, const Message& m) {
    int64_t uid = 0;
    if (!requireAuth(c, m, MsgType::PLACE_STONE_RESP, uid)) return;
    const int x = jint(m.data, "x", -1), y = jint(m.data, "y", -1);
    if (!Validator::validateCoordinates(x, y)) {
        replyError(c, m, MsgType::PLACE_STONE_RESP, ErrCode::INVALID_PARAM, "坐标无效");
        return;
    }
    // 关键：必须先取房间号/颜色再落子。placeStone 内部 finalize_ 会在对局结束时
    // 清除 uidToRoom_ 映射，之后 roomOf(uid)/colorOf(uid) 会失效，
    // 导致 game_over_notify 广播到房间 0（无人接收）。
    const int color = rooms_.colorOf(uid);
    const int64_t rid = rooms_.roomOf(uid);
    GameOverEvent over;
    StoneResult r = rooms_.placeStone(uid, x, y, &over);
    if (!r.ok) {
        replyError(c, m, MsgType::PLACE_STONE_RESP, ErrCode::INVALID_PARAM,
                   r.error.empty() ? "落子失败" : r.error);
        return;
    }
    reply(c, m, MsgType::PLACE_STONE_RESP, {{"code", ErrCode::OK}});
    json notify = {{"uid", uid}, {"x", x}, {"y", y}, {"color", color},
                   {"total_time_left", r.totalTimeLeft}, {"step_time_left", r.stepTimeLeft},
                   {"is_game_over", r.gameOver}};
    broadcastRoom(rid, MsgType::PLACE_STONE_NOTIFY, notify);
    if (r.gameOver) {
        json overData = {{"winner_uid", over.winnerUid}, {"reason", over.reasonText},
                         {"elo_change", json::object()}};
        overData["elo_change"][std::to_string(over.blackUid)] = over.eloBlackDelta;
        overData["elo_change"][std::to_string(over.whiteUid)] = over.eloWhiteDelta;
        broadcastRoom(rid, MsgType::GAME_OVER_NOTIFY, overData);
    }
}

void BizHandler::handleUndo(IConnection* c, const Message& m) {
    int64_t uid = 0;
    if (!requireAuth(c, m, MsgType::UNDO_RESP, uid)) return;
    // data.accepted 存在 -> 回应；否则 -> 请求
    if (m.data.contains("accepted")) {
        const int code = rooms_.respondUndo(uid, jbool(m.data, "accepted", false));
        reply(c, m, MsgType::UNDO_RESP, {{"code", code},
              {"accepted", jbool(m.data, "accepted", false)}});
    } else {
        const int code = rooms_.requestUndo(uid);
        reply(c, m, MsgType::UNDO_RESP, {{"code", code}, {"message", "悔棋请求已发送"}});
    }
}

void BizHandler::handleDraw(IConnection* c, const Message& m) {
    int64_t uid = 0;
    if (!requireAuth(c, m, MsgType::DRAW_RESP, uid)) return;
    if (m.data.contains("accepted")) {
        const bool accepted = jbool(m.data, "accepted", false);
        const int code = rooms_.respondDraw(uid, accepted);
        reply(c, m, MsgType::DRAW_RESP, {{"code", code}, {"accepted", accepted}});
        // 若接受，respondDraw 内部已 finalize；广播和棋结束
        if (code == ErrCode::OK && accepted) {
            const int64_t rid = rooms_.roomOf(uid);
            broadcastRoom(rid, MsgType::GAME_OVER_NOTIFY,
                          {{"winner_uid", 0}, {"reason", "draw"}, {"elo_change", json::object()}});
        }
    } else {
        const int code = rooms_.requestDraw(uid);
        reply(c, m, MsgType::DRAW_RESP, {{"code", code}, {"message", "和棋请求已发送"}});
    }
}

void BizHandler::handleSurrender(IConnection* c, const Message& m) {
    int64_t uid = 0;
    if (!requireAuth(c, m, MsgType::GAME_OVER_NOTIFY, uid)) return;
    GameOverEvent over;
    const int code = rooms_.surrender(uid, over);
    if (code != ErrCode::OK) {
        replyError(c, m, MsgType::GAME_OVER_NOTIFY, code, "认输失败");
        return;
    }
    json data = {{"winner_uid", over.winnerUid}, {"reason", "surrender"},
                 {"elo_change", json::object()}};
    data["elo_change"][std::to_string(over.blackUid)] = over.eloBlackDelta;
    data["elo_change"][std::to_string(over.whiteUid)] = over.eloWhiteDelta;
    broadcastRoom(over.roomId, MsgType::GAME_OVER_NOTIFY, data);
}

// ---------------- 商城 ----------------

void BizHandler::handleShopList(IConnection* c, const Message& m) {
    int64_t uid = 0;
    if (!requireAuth(c, m, MsgType::SHOP_LIST_RESP, uid)) return;
    std::vector<ShopItem> items;
    db_.queryShopItems(items);
    std::vector<UserItem> owned;
    db_.queryUserItems(uid, owned);
    json arr = json::array();
    for (const auto& it : items) {
        bool has=false, eq=false;
        for (const auto& o : owned) if (o.itemId == it.itemId) { has=true; eq=o.equipped; break; }
        // 下发预览图路径（qrc 里的 :/res/... 资源），客户端据此显示商品图
        arr.push_back({{"id", it.itemId}, {"name", it.name}, {"type", it.type},
                       {"price", it.price}, {"owned", has}, {"equipped", eq},
                       {"preview_image", it.resourcePath}});
    }
    reply(c, m, MsgType::SHOP_LIST_RESP, {{"code", ErrCode::OK}, {"items", arr}});
}

void BizHandler::handleBuyItem(IConnection* c, const Message& m) {
    int64_t uid = 0;
    if (!requireAuth(c, m, MsgType::BUY_ITEM_RESP, uid)) return;
    const int64_t itemId = jint(m.data, "item_id", 0);
    if (itemId <= 0) {
        replyError(c, m, MsgType::BUY_ITEM_RESP, ErrCode::INVALID_PARAM, "商品ID无效");
        return;
    }
    const bool ok = db_.buyItem(uid, itemId);
    reply(c, m, MsgType::BUY_ITEM_RESP,
          ok ? json{{"code", ErrCode::OK}, {"message", "购买成功"}}
             : json{{"code", ErrCode::INVALID_PARAM}, {"message", "金币不足或已拥有"}});
    security_.logSensitiveOperation(uid, "buy", "item=" + std::to_string(itemId) + (ok ? " ok" : " fail"));
}

void BizHandler::handleEquipItem(IConnection* c, const Message& m) {
    int64_t uid = 0;
    if (!requireAuth(c, m, MsgType::EQUIP_ITEM_RESP, uid)) return;
    const int64_t itemId = jint(m.data, "item_id", 0);
    const bool equipped = jbool(m.data, "equipped", true);
    const bool ok = db_.equipItem(uid, itemId, equipped);
    reply(c, m, MsgType::EQUIP_ITEM_RESP,
          ok ? json{{"code", ErrCode::OK}} : json{{"code", ErrCode::INVALID_PARAM}});
    security_.logSensitiveOperation(uid, "equip", "item=" + std::to_string(itemId));
}

// ---------------- 心跳 / 重连 ----------------

void BizHandler::handleHeartbeat(IConnection* c, const Message& m) {
    const int64_t ts = jint(m.data, "timestamp", static_cast<int>(nowSec()));
    if (c->uid() > 0) redis_.set(redis_.onlineKey(c->uid()), "1", 90);
    reply(c, m, MsgType::HEARTBEAT_RESP, {{"timestamp", ts}});
}

void BizHandler::handleReconnect(IConnection* c, const Message& m) {
    const std::string token = jstr(m.data, "token");
    int64_t uid = 0;
    if (token.empty() || !sessions_.validateSession(token, uid)) {
        replyError(c, m, MsgType::RECONNECT_RESP, ErrCode::TOKEN_INVALID, "会话已失效");
        return;
    }
    c->setUid(uid);
    {
        std::lock_guard<std::mutex> lk(connMtx_);
        connStates_[c->id()] = {uid, token};
    }
    {
        std::lock_guard<std::mutex> lk(uidMtx_);
        uidConns_[uid] = c;
    }
    UserInfo info; rooms_.userInfo(uid, info);
    reply(c, m, MsgType::RECONNECT_RESP,
          {{"code", ErrCode::OK}, {"uid", uid}, {"user_info", userInfoJson(info)}});
}

// ---------------- 周期扫描 ----------------

void BizHandler::tick() {
    std::vector<GameOverEvent> evs;
    rooms_.scanTimeouts(evs);
    for (const auto& ev : evs) {
        json timeoutData = {{"timeout_uid", ev.timeoutUid},
                            {"reason", ev.timeoutReason},
                            {"winner_uid", ev.winnerUid}};
        broadcastRoom(ev.roomId, MsgType::TIMEOUT_NOTIFY, timeoutData);
        json overData = {{"winner_uid", ev.winnerUid}, {"reason", "timeout"},
                         {"elo_change", json::object()}};
        overData["elo_change"][std::to_string(ev.blackUid)] = ev.eloBlackDelta;
        overData["elo_change"][std::to_string(ev.whiteUid)] = ev.eloWhiteDelta;
        broadcastRoom(ev.roomId, MsgType::GAME_OVER_NOTIFY, overData);
    }
}

} // namespace AIWZQ

// ==================== 对外唯一契约 ====================
extern "C" AIWZQ::IMessageHandler* createBizHandler() {
    return new AIWZQ::BizHandler();
}
