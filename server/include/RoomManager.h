#pragma once
/**
 * RoomManager.h — 房间管理与对局结算
 * 对应设计文档：01-服务端设计.md 2.4/2.5；04-数据库设计.md 3.2/四（ELO）
 *               03-通信协议设计.md 4.2/4.3（房间列表、创建/加入/准备/落子/悔棋/和棋/认输/超时）
 */
#include "Database.h"
#include "RedisClient.h"
#include "Room.h"
#include "Types.h"
#include "Protocol.h"
#include "GameDefs.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace AIWZQ {

// 对局结束事件（BizHandler 据此广播 game_over_notify / timeout_notify）
struct GameOverEvent {
    int64_t     roomId = 0;
    int64_t     winnerUid = 0;             // 0 = 和局
    GameEndReason reason = REASON_FIVE;
    std::string reasonText = "win";       // win|draw|surrender|timeout
    int64_t     blackUid = 0;
    int64_t     whiteUid = 0;
    int         eloBlackDelta = 0;
    int         eloWhiteDelta = 0;
    int64_t     timeoutUid = 0;            // 仅 timeout 有效
    std::string timeoutReason;             // step_timeout | total_timeout
};

class RoomManager {
public:
    RoomManager(Database& db, RedisClient& redis);

    // ---- 房间列表/创建/加入/匹配 ----
    // 分页房间列表：total 为总数，rooms 每个元素含 room_id/owner_name/owner_elo/status/has_password/create_time
    void listRooms(int page, int pageSize, json& outRooms, int& total);
    int64_t createRoom(int64_t ownerUid, int totalTimeSec, int stepTimeSec,
                       const std::string& password);
    // 返回 0 成功；非 0 为 ErrCode
    int joinRoom(int64_t roomId, int64_t uid, const std::string& password);
    int64_t randomMatch(int64_t uid);     // 加入已有 waiting 房间或新建；返回 roomId
    bool leaveRoom(int64_t uid);

    // ---- 对局操作 ----
    int ready(int64_t uid, bool ready, GameOverEvent* startEvent = nullptr);
    StoneResult placeStone(int64_t uid, int x, int y, GameOverEvent* over = nullptr);
    // 悔棋请求：首次记录待确认，第二次由对方确认后执行；返回 ErrCode
    int requestUndo(int64_t uid);
    int respondUndo(int64_t uid, bool accepted);
    int requestDraw(int64_t uid);
    int respondDraw(int64_t uid, bool accepted);
    int surrender(int64_t uid, GameOverEvent& over);

    // 超时扫描：把触发的结束事件放入 out
    void scanTimeouts(std::vector<GameOverEvent>& out);

    // 玩家所在房间
    int64_t roomOf(int64_t uid) const;
    // 玩家在房间中的颜色（1=黑 2=白），不在房间返回 0
    int colorOf(int64_t uid) const;

    // 排行榜（Redis ZSET）
    void rebuildRankCache();
    bool rankList(const std::string& rankType, int page, int pageSize, json& out);

    // 查询用户信息（包装 Database，供 lobby 展示）
    bool userInfo(int64_t uid, UserInfo& out);

    // 更新 ELO 缓存
    void updateRankCache(int64_t uid, int32_t elo);

private:
    std::shared_ptr<Room> findRoom_(int64_t roomId) const;
    void finalize_(const std::shared_ptr<Room>& room, GameOverEvent& ev,
                   int winnerColor, GameEndReason reason, const std::string& reasonText);
    int kFactor_(int elo) const;
    void pushEvent_(const GameOverEvent& ev);

    Database& db_;
    RedisClient& redis_;
    mutable std::mutex mtx_;
    std::map<int64_t, std::shared_ptr<Room>> rooms_;
    std::map<int64_t, int64_t> uidToRoom_;
    std::atomic<int64_t> nextRoomId_{1001};

    // 待确认请求：uid -> {roomId, targetUid}
    struct Pending { int64_t roomId; int64_t targetUid; };
    std::map<int64_t, Pending> undoPending_;
    std::map<int64_t, Pending> drawPending_;
    std::mutex pendingMtx_;

    // 最近结束事件队列（BizHandler 每轮取走广播）
    std::vector<GameOverEvent> events_;
    std::mutex eventsMtx_;
};

} // namespace AIWZQ
