#pragma once
/**
 * Database.h — MySQL C API 封装（连接管理 + 参数化 CRUD）
 * 对应设计文档：01-服务端设计.md 2.3/2.7（用户/对局/商城数据访问方法）
 *               04-数据库设计.md 3.1~3.8（表结构）
 *               06-加密系统设计.md 3.2（参数化查询防 SQL 注入）
 */
#include "Config.h"
#include "GameDefs.h"
#include "Types.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// mysql C API 的不完整前置声明：避免头文件依赖 <mysql/mysql.h>，
// 使本机无 MySQL 开发头时本头文件仍可编译；实现文件才 include 真实头。
struct st_mysql;
struct st_mysql_stmt;
typedef struct st_mysql MYSQL;
typedef struct st_mysql_stmt MYSQL_STMT;

namespace AIWZQ {

// 注册/更新用的用户实体（对应 t_user / 设计文档 2.3 的 User）
struct User {
    int64_t     uid = 0;
    std::string username;
    std::string passwordHash;            // Argon2id PHC 串
    std::string salt;
    std::string email;
    std::string avatar = "default";
    int64_t     coins = 0;
    int32_t     eloScore = 1500;
    int32_t     wins = 0;
    int32_t     losses = 0;
    int32_t     draws = 0;
    int32_t     totalGames = 0;
    int8_t      status = 1;
};

// 对局记录（对应 t_game_record）
struct GameRecord {
    int64_t     roomId = 0;
    int64_t     blackUid = 0;
    int64_t     whiteUid = 0;
    std::optional<int64_t> winnerUid;    // nullopt = 和局
    int         endReason = 0;           // GameEndReason
    int         blackTimeUsed = 0;
    int         whiteTimeUsed = 0;
    std::string movesJson = "[]";
    std::string startTime;               // 'YYYY-MM-DD HH:MM:SS'
    std::string endTime;
};

// 商城商品（对应 t_shop_item）
struct ShopItem {
    int64_t     itemId = 0;
    std::string name;
    std::string description;
    std::string type;                    // board/stone/effect/avatar
    uint32_t    price = 0;
    std::string resourcePath;
    bool        isHot = false;
};

// 用户持有道具（对应 t_user_item + join t_shop_item 的展示字段）
struct UserItem {
    int64_t     id = 0;
    int64_t     itemId = 0;
    std::string name;
    std::string type;
    uint32_t    price = 0;
    std::string resourcePath;
    bool        equipped = false;
};

class Database {
public:
    Database();
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // 使用全局 Config::instance().database() 建连；失败返回 false。
    bool connect();
    bool isConnected() const { return conn_ != nullptr; }
    void close();

    // ---- 用户相关（2.7）----
    bool insertUser(const User& user);
    std::optional<User> queryUserByUsername(const std::string& username);
    std::optional<User> queryUserByUid(int64_t uid);
    bool updateUser(const User& user);
    bool updatePassword(int64_t uid, const std::string& passwordHash);
    // 登录失败计数：失败 +1；成功清零。返回更新后是否成功。
    bool updateLoginAttempts(int64_t uid, int attempts);

    // ---- 对局记录（2.7）----
    bool insertGameRecord(const GameRecord& record);
    bool updateUserStats(int64_t uid, GameResult result, int32_t eloDelta);

    // ---- 商城（2.7）----
    bool queryShopItems(std::vector<ShopItem>& items);
    bool queryUserItems(int64_t uid, std::vector<UserItem>& items);
    bool buyItem(int64_t uid, int64_t itemId);       // 事务：扣金币 + 写 t_user_item
    bool equipItem(int64_t uid, int64_t itemId, bool equipped);

    // ---- 安全审计日志（2.8.3）----
    bool insertSecurityLog(int64_t uid, const std::string& ip,
                           const std::string& action, int status,
                           const std::string& details);

    // ---- 排行榜（从 DB 拉全量，供 Redis ZSET 回填）----
    bool queryAllElo(std::vector<std::pair<int64_t, int32_t>>& out);

private:
    // 执行无返回集的参数化语句；stmt 需已绑定参数。
    bool exec_(MYSQL_STMT* stmt);
    void closeUnlocked_();
    MYSQL* conn_ = nullptr;
    // MYSQL* 连接非线程安全：网络层多 IO 线程并发调用业务层，统一串行化。
    mutable std::mutex mtx_;

    bool ensureConnected();
};

} // namespace AIWZQ
