#pragma once
#include <cstdint>
#include <string>

namespace AIWZQ {

// ---------- 棋子颜色 ----------
namespace Color {
    constexpr int EMPTY = 0;
    constexpr int BLACK = 1;
    constexpr int WHITE = 2;
}

// ---------- 棋盘尺寸 ----------
constexpr int BOARD_SIZE = 15;

// ---------- 消息类型常量（与服务端设计文档 2.2 一致） ----------
namespace MsgType {
    // 用户系统
    constexpr const char* REGISTER_REQ      = "register_req";
    constexpr const char* REGISTER_RESP     = "register_resp";
    constexpr const char* LOGIN_REQ         = "login_req";
    constexpr const char* LOGIN_RESP        = "login_resp";
    constexpr const char* LOGOUT_REQ        = "logout_req";
    constexpr const char* LOGOUT_RESP       = "logout_resp";
    constexpr const char* FIND_PWD_REQ      = "find_pwd_req";
    constexpr const char* FIND_PWD_RESP     = "find_pwd_resp";
    constexpr const char* CHANGE_PWD_REQ    = "change_pwd_req";
    constexpr const char* CHANGE_PWD_RESP   = "change_pwd_resp";

    // 大厅
    constexpr const char* ROOM_LIST_REQ     = "room_list_req";
    constexpr const char* ROOM_LIST_RESP    = "room_list_resp";
    constexpr const char* PLAYER_LIST_REQ   = "player_list_req";
    constexpr const char* PLAYER_LIST_RESP  = "player_list_resp";
    constexpr const char* HALL_CHAT_REQ     = "hall_chat_req";
    constexpr const char* HALL_CHAT_NOTIFY  = "hall_chat_notify";
    constexpr const char* RANK_LIST_REQ     = "rank_list_req";
    constexpr const char* RANK_LIST_RESP    = "rank_list_resp";

    // 房间
    constexpr const char* CREATE_ROOM_REQ   = "create_room_req";
    constexpr const char* CREATE_ROOM_RESP  = "create_room_resp";
    constexpr const char* JOIN_ROOM_REQ     = "join_room_req";
    constexpr const char* JOIN_ROOM_RESP    = "join_room_resp";
    constexpr const char* RANDOM_JOIN_REQ   = "random_join_req";
    constexpr const char* ROOM_INFO_RESP    = "room_info_resp";
    constexpr const char* READY_REQ         = "ready_req";
    constexpr const char* READY_RESP        = "ready_resp";
    constexpr const char* GAME_START_NOTIFY = "game_start_notify";

    // 对局
    constexpr const char* PLACE_STONE_REQ   = "place_stone_req";
    constexpr const char* PLACE_STONE_RESP  = "place_stone_resp";
    constexpr const char* PLACE_STONE_NOTIFY= "place_stone_notify";
    constexpr const char* UNDO_REQ          = "undo_req";
    constexpr const char* UNDO_RESP         = "undo_resp";
    constexpr const char* DRAW_REQ          = "draw_req";
    constexpr const char* DRAW_RESP         = "draw_resp";
    constexpr const char* SURRENDER_REQ     = "surrender_req";
    constexpr const char* GAME_OVER_NOTIFY  = "game_over_notify";
    constexpr const char* TIMEOUT_NOTIFY    = "timeout_notify";

    // 商城
    constexpr const char* SHOP_LIST_REQ     = "shop_list_req";
    constexpr const char* SHOP_LIST_RESP    = "shop_list_resp";
    constexpr const char* BUY_ITEM_REQ      = "buy_item_req";
    constexpr const char* BUY_ITEM_RESP     = "buy_item_resp";
    constexpr const char* EQUIP_ITEM_REQ    = "equip_item_req";
    constexpr const char* EQUIP_ITEM_RESP   = "equip_item_resp";

    // 心跳与重连
    constexpr const char* HEARTBEAT_REQ     = "heartbeat_req";
    constexpr const char* HEARTBEAT_RESP    = "heartbeat_resp";
    constexpr const char* RECONNECT_REQ     = "reconnect_req";
    constexpr const char* RECONNECT_RESP    = "reconnect_resp";
}

// ---------- 业务错误码（与通信协议文档 4.1 一致） ----------
namespace ErrCode {
    constexpr int OK                  = 0;
    constexpr int USER_EXISTS         = 1001;
    constexpr int EMAIL_EXISTS        = 1002;
    constexpr int BAD_CREDENTIALS     = 1003;
    constexpr int TOKEN_INVALID       = 1004;
    constexpr int VERIFY_CODE_WRONG   = 1005;
    constexpr int RATE_LIMITED        = 1006;
    constexpr int ACCOUNT_LOCKED      = 1007;
    constexpr int INVALID_PARAM       = 1008;
}

// ---------- 通用结构 ----------
struct Point {
    int x;
    int y;
    Point(int px = -1, int py = -1) : x(px), y(py) {}
    bool valid() const { return x >= 0 && x < BOARD_SIZE && y >= 0 && y < BOARD_SIZE; }
    bool operator==(const Point& o) const { return x == o.x && y == o.y; }
};

// 对局结束原因（与 t_game_record.end_reason 对应）
enum GameEndReason {
    REASON_FIVE   = 1,  // 五连
    REASON_DRAW   = 2,  // 和棋
    REASON_SURRENDER = 3, // 认输
    REASON_TIMEOUT = 4  // 超时
};

// 用户公开信息（登录/排行榜注入 data.user_info）
struct UserInfo {
    int64_t uid = 0;
    std::string username;
    int32_t eloScore = 1500;
    int32_t wins = 0;
    int32_t losses = 0;
    int32_t draws = 0;
    int64_t coins = 0;
    std::string avatar = "default";
};

} // namespace AIWZQ
