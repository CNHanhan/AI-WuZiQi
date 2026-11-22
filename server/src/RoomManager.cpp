/**
 * RoomManager.cpp — 房间管理、ELO 结算、超时扫描
 * 对应设计文档：01-服务端设计.md 2.4/2.5；04-数据库设计.md 3.2/四；03-通信协议设计.md 4.2/4.3
 */
#include "RoomManager.h"
#include "Config.h"
#include "Database.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace AIWZQ {

namespace {

// 把 snapshot kv 数组转成 json
json snapshotToJson(const std::vector<std::pair<std::string, std::string>>& kv) {
    json j = json::object();
    for (const auto& p : kv) {
        if (p.first == "black_uid" || p.first == "white_uid" ||
            p.first == "current_turn" || p.first == "room_id" ||
            p.first == "black_time_left" || p.first == "white_time_left")
            j[p.first] = std::stoll(p.second);
        else if (p.first == "has_password")
            j[p.first] = (p.second == "1");
        else
            j[p.first] = p.second;
    }
    return j;
}

} // namespace

RoomManager::RoomManager(Database& db, RedisClient& redis) : db_(db), redis_(redis) {}

std::shared_ptr<Room> RoomManager::findRoom_(int64_t roomId) const {
    auto it = rooms_.find(roomId);
    return it == rooms_.end() ? nullptr : it->second;
}

int64_t RoomManager::roomOf(int64_t uid) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = uidToRoom_.find(uid);
    return it == uidToRoom_.end() ? 0 : it->second;
}

int RoomManager::colorOf(int64_t uid) const {
    auto room = findRoom_(roomOf(uid));
    if (!room) return 0;
    if (uid == room->blackUid()) return 1;
    if (uid == room->whiteUid()) return 2;
    return 0;
}

void RoomManager::listRooms(int page, int pageSize, json& outRooms, int& total) {
    if (page < 1) page = 1;
    if (pageSize < 1) pageSize = 20;
    if (pageSize > 100) pageSize = 100;
    std::vector<std::pair<int64_t, json>> all;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (const auto& kv : rooms_) {
            auto st = kv.second->status();
            if (st == RoomStatus::FINISHED) continue;
            json j = snapshotToJson(kv.second->snapshot());
            int64_t owner = kv.second->ownerUid();
            auto u = db_.queryUserByUid(owner);
            j["owner_name"] = u ? u->username : std::string("?");
            j["owner_elo"] = u ? u->eloScore : 1500;
            j["room_id"] = kv.first;
            all.emplace_back(kv.first, j);
        }
    }
    std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    total = static_cast<int>(all.size());
    outRooms = json::array();
    const size_t begin = static_cast<size_t>(page - 1) * pageSize;
    for (size_t i = begin; i < all.size() && i < begin + pageSize; ++i)
        outRooms.push_back(all[i].second);
}

int64_t RoomManager::createRoom(int64_t ownerUid, int totalTimeSec, int stepTimeSec,
                                const std::string& password) {
    if (totalTimeSec <= 0) totalTimeSec = Config::instance().game().totalTimeSec;
    if (stepTimeSec <= 0) stepTimeSec = Config::instance().game().stepTimeSec;
    const int64_t id = nextRoomId_.fetch_add(1);
    auto room = std::make_shared<Room>(id, totalTimeSec, stepTimeSec, password);
    room->onPlayerJoin(ownerUid);
    {
        std::lock_guard<std::mutex> lk(mtx_);
        rooms_[id] = room;
        uidToRoom_[ownerUid] = id;
    }
    // 同步房间状态到 Redis hash
    for (const auto& kv : room->snapshot())
        redis_.hset(redis_.roomKey(id), kv.first, kv.second);
    return id;
}

int RoomManager::joinRoom(int64_t roomId, int64_t uid, const std::string& password) {
    auto room = findRoom_(roomId);
    if (!room) return ErrCode::INVALID_PARAM;
    if (room->status() != RoomStatus::WAITING) return ErrCode::INVALID_PARAM;
    if (room->hasPassword() && room->password() != password) return ErrCode::BAD_CREDENTIALS;
    if (!room->onPlayerJoin(uid)) return ErrCode::INVALID_PARAM;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        uidToRoom_[uid] = roomId;
    }
    for (const auto& kv : room->snapshot())
        redis_.hset(redis_.roomKey(roomId), kv.first, kv.second);
    return ErrCode::OK;
}

int64_t RoomManager::randomMatch(int64_t uid) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (const auto& kv : rooms_) {
            auto r = kv.second;
            if (r->status() == RoomStatus::WAITING && r->isFull() == false && r->ownerUid() != uid) {
                if (r->onPlayerJoin(uid)) {
                    uidToRoom_[uid] = kv.first;
                    return kv.first;
                }
            }
        }
    }
    return createRoom(uid, 0, 0, "");
}

bool RoomManager::leaveRoom(int64_t uid) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = uidToRoom_.find(uid);
    if (it == uidToRoom_.end()) return false;
    uidToRoom_.erase(it);
    return true;
}

int RoomManager::ready(int64_t uid, bool ready, GameOverEvent* startEvent) {
    const int64_t rid = roomOf(uid);
    auto room = findRoom_(rid);
    if (!room) return ErrCode::INVALID_PARAM;
    if (!room->onPlayerReady(uid, ready)) return ErrCode::INVALID_PARAM;
    if (startEvent && room->status() == RoomStatus::PLAYING) {
        startEvent->roomId = rid;
        startEvent->winnerUid = 0;
        startEvent->blackUid = room->blackUid();
        startEvent->whiteUid = room->whiteUid();
        startEvent->reason = REASON_FIVE;
        startEvent->reasonText = "start";
    }
    return ErrCode::OK;
}

StoneResult RoomManager::placeStone(int64_t uid, int x, int y, GameOverEvent* over) {
    const int64_t rid = roomOf(uid);
    auto room = findRoom_(rid);
    if (!room) { StoneResult r; r.error = "not_in_room"; return r; }
    StoneResult r = room->onStonePlaced(uid, x, y);
    if (r.ok && r.gameOver) {
        GameOverEvent ev;
        finalize_(room, ev, r.winnerColor, r.endReason,
                  r.endReason == REASON_DRAW ? "draw" : "win");
        r.totalTimeLeft = (room->blackTimeLeft());
        if (over) *over = ev;
    }
    return r;
}

int RoomManager::kFactor_(int elo) const {
    if (elo < 2000) return 32;
    if (elo < 2400) return 24;
    return 16;
}

void RoomManager::finalize_(const std::shared_ptr<Room>& room, GameOverEvent& ev,
                            int winnerColor, GameEndReason reason,
                            const std::string& reasonText) {
    ev.roomId = room->roomId();
    ev.blackUid = room->blackUid();
    ev.whiteUid = room->whiteUid();
    ev.reason = reason;
    ev.reasonText = reasonText;
    ev.winnerUid = (winnerColor == Color::BLACK) ? ev.blackUid :
                   (winnerColor == Color::WHITE) ? ev.whiteUid : 0;

    auto black = db_.queryUserByUid(ev.blackUid);
    auto white = db_.queryUserByUid(ev.whiteUid);
    if (black && white) {
        const double Ra = black->eloScore, Rb = white->eloScore;
        const double Ea = 1.0 / (1.0 + std::pow(10.0, (Rb - Ra) / 400.0));
        const double Eb = 1.0 - Ea;
        double Sa = 0.5, Sb = 0.5;
        if (winnerColor == Color::BLACK) { Sa = 1.0; Sb = 0.0; }
        else if (winnerColor == Color::WHITE) { Sa = 0.0; Sb = 1.0; }
        const int ka = kFactor_(black->eloScore), kb = kFactor_(white->eloScore);
        ev.eloBlackDelta = static_cast<int>(std::lround(ka * (Sa - Ea)));
        ev.eloWhiteDelta = static_cast<int>(std::lround(kb * (Sb - Eb)));

        db_.updateUserStats(ev.blackUid,
            winnerColor == Color::BLACK ? GameResult::BLACK_WIN :
            winnerColor == Color::WHITE ? GameResult::WHITE_WIN : GameResult::DRAW,
            ev.eloBlackDelta);
        db_.updateUserStats(ev.whiteUid,
            winnerColor == Color::BLACK ? GameResult::WHITE_WIN :
            winnerColor == Color::WHITE ? GameResult::BLACK_WIN : GameResult::DRAW,
            ev.eloWhiteDelta);
        updateRankCache(ev.blackUid, black->eloScore + ev.eloBlackDelta);
        updateRankCache(ev.whiteUid, white->eloScore + ev.eloWhiteDelta);
    }

    // 写对局记录
    GameRecord rec;
    rec.roomId = ev.roomId;
    rec.blackUid = ev.blackUid;
    rec.whiteUid = ev.whiteUid;
    if (ev.winnerUid) rec.winnerUid = ev.winnerUid;
    rec.endReason = static_cast<int>(reason);
    rec.blackTimeUsed = room->totalTimeSec() - room->blackTimeLeft();
    rec.whiteTimeUsed = room->totalTimeSec() - room->whiteTimeLeft();
    rec.movesJson = room->movesJson();
    db_.insertGameRecord(rec);

    redis_.hset(redis_.roomKey(room->roomId()), "status", "finished");
    pushEvent_(ev);
}

void RoomManager::pushEvent_(const GameOverEvent& ev) {
    std::lock_guard<std::mutex> lk(eventsMtx_);
    events_.push_back(ev);
}

void RoomManager::scanTimeouts(std::vector<GameOverEvent>& out) {
    std::vector<std::shared_ptr<Room>> snapshot;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (const auto& kv : rooms_) snapshot.push_back(kv.second);
    }
    for (auto& room : snapshot) {
        TimeoutResult t = room->onTimeout();
        if (!t.fired) continue;
        GameOverEvent ev;
        ev.timeoutUid = t.timeoutUid;
        ev.timeoutReason = t.reason;
        finalize_(room, ev,
                  (t.winnerUid == room->blackUid()) ? Color::BLACK : Color::WHITE,
                  REASON_TIMEOUT, "timeout");
        out.push_back(ev);
    }
}

// ---- 悔棋 / 和棋 ----

int RoomManager::requestUndo(int64_t uid) {
    const int64_t rid = roomOf(uid);
    auto room = findRoom_(rid);
    if (!room || room->status() != RoomStatus::PLAYING) return ErrCode::INVALID_PARAM;
    const int64_t other = (uid == room->blackUid()) ? room->whiteUid() : room->blackUid();
    std::lock_guard<std::mutex> lk(pendingMtx_);
    undoPending_[other] = {rid, uid};
    return ErrCode::OK;
}

int RoomManager::respondUndo(int64_t uid, bool accepted) {
    int64_t requester = 0;
    {
        std::lock_guard<std::mutex> lk(pendingMtx_);
        auto it = undoPending_.find(uid);
        if (it == undoPending_.end()) return ErrCode::INVALID_PARAM;
        requester = it->second.targetUid;
        undoPending_.erase(it);
    }
    if (!accepted) return ErrCode::OK;
    auto room = findRoom_(roomOf(requester));
    if (!room || !room->undoStone(requester)) return ErrCode::INVALID_PARAM;
    return ErrCode::OK;
}

int RoomManager::requestDraw(int64_t uid) {
    const int64_t rid = roomOf(uid);
    auto room = findRoom_(rid);
    if (!room || room->status() != RoomStatus::PLAYING) return ErrCode::INVALID_PARAM;
    const int64_t other = (uid == room->blackUid()) ? room->whiteUid() : room->blackUid();
    std::lock_guard<std::mutex> lk(pendingMtx_);
    drawPending_[other] = {rid, uid};
    return ErrCode::OK;
}

int RoomManager::respondDraw(int64_t uid, bool accepted) {
    int64_t requester = 0;
    {
        std::lock_guard<std::mutex> lk(pendingMtx_);
        auto it = drawPending_.find(uid);
        if (it == drawPending_.end()) return ErrCode::INVALID_PARAM;
        requester = it->second.targetUid;
        drawPending_.erase(it);
    }
    if (!accepted) return ErrCode::OK;
    auto room = findRoom_(roomOf(requester));
    if (!room) return ErrCode::INVALID_PARAM;
    GameOverEvent ev;
    finalize_(room, ev, Color::EMPTY, REASON_DRAW, "draw");
    return ErrCode::OK;
}

int RoomManager::surrender(int64_t uid, GameOverEvent& over) {
    const int64_t rid = roomOf(uid);
    auto room = findRoom_(rid);
    if (!room || room->status() != RoomStatus::PLAYING) return ErrCode::INVALID_PARAM;
    const int winnerColor = (uid == room->blackUid()) ? Color::WHITE : Color::BLACK;
    room->finishSurrender(uid);
    finalize_(room, over, winnerColor, REASON_SURRENDER, "surrender");
    return ErrCode::OK;
}

// ---- 排行榜 / 用户信息 ----

void RoomManager::rebuildRankCache() {
    std::vector<std::pair<int64_t, int32_t>> rows;
    if (!db_.queryAllElo(rows)) return;
    for (const auto& r : rows) {
        redis_.zadd("rank:elo", static_cast<double>(r.second), std::to_string(r.first));
    }
}

void RoomManager::updateRankCache(int64_t uid, int32_t elo) {
    redis_.zadd("rank:elo", static_cast<double>(elo), std::to_string(uid));
}

bool RoomManager::rankList(const std::string& rankType, int page, int pageSize, json& out) {
    if (page < 1) page = 1;
    if (pageSize < 1) pageSize = 20;
    const std::string key = (rankType == "wins") ? "rank:wins" : "rank:elo";
    std::vector<std::string> rows;
    redis_.zrange(key, (page - 1) * pageSize, page * pageSize - 1, rows);
    out = json::array();
    int rank = (page - 1) * pageSize + 1;
    for (size_t i = 0; i < rows.size(); ++i) {
        int64_t uid = 0;
        try { uid = std::stoll(rows[i]); } catch (...) { continue; }
        auto u = db_.queryUserByUid(uid);
        if (!u) continue;
        json e;
        e["rank"] = rank++;
        e["username"] = u->username;
        e["elo_score"] = u->eloScore;
        e["wins"] = u->wins;
        out.push_back(e);
    }
    return true;
}

bool RoomManager::userInfo(int64_t uid, UserInfo& out) {
    auto u = db_.queryUserByUid(uid);
    if (!u) return false;
    out.uid = u->uid;
    out.username = u->username;
    out.eloScore = u->eloScore;
    out.wins = u->wins;
    out.losses = u->losses;
    out.draws = u->draws;
    out.coins = u->coins;
    out.avatar = u->avatar;
    return true;
}

} // namespace AIWZQ
