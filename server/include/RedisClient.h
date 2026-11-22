#pragma once
/**
 * RedisClient.h — hiredis 封装
 * 对应设计文档：04-数据库设计.md 3.6~3.7（Redis key 设计）
 *               01-服务端设计.md 2.8（会话/排行榜/频率限制）
 *               06-加密系统设计.md 3.1（Redis 密码从环境变量读取）
 *
 * 业务 key 约定：
 *   session:{token}       STRING  value=uid  TTL=7200
 *   rank:elo / rank:wins  ZSET    member=uid
 *   user:{uid}            HASH    用户缓存
 *   online:{uid}          STRING  TTL=90（心跳续期）
 *   room:{id}            HASH    房间状态
 *   login_fail:{ip|username} STRING  失败次数 TTL=900
 */
#include "Config.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct redisContext;

namespace AIWZQ {

struct RedisReply {
    bool           ok = false;            // 命令是否成功执行（非错误、非 nil）
    bool           nil = false;
    int64_t        integer = 0;
    std::string    str;
    std::vector<std::string> array;
    std::map<std::string, std::string> hash; // hgetall 结果
};

class RedisClient {
public:
    RedisClient();
    ~RedisClient();

    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    bool connect();
    bool isConnected() const { return ctx_ != nullptr; }
    void close();

    // ---- 基础命令 ----
    bool get(const std::string& key, std::string& value);
    bool set(const std::string& key, const std::string& value,
             int ttlSeconds = -1);         // ttl<0 表示永不过期；SET EX 使用原子命令
    bool del(const std::string& key);
    bool exists(const std::string& key);
    bool expire(const std::string& key, int ttlSeconds);
    bool incr(const std::string& key, int64_t& newValue);

    // ---- ZSET ----
    bool zadd(const std::string& key, double score, const std::string& member);
    bool zscore(const std::string& key, const std::string& member, double& score);
    // 降序分页；withScores=true 时 array 交替返回 member/score 字符串
    bool zrange(const std::string& key, int start, int stop,
                std::vector<std::string>& out, bool withScores = false);

    // ---- HASH ----
    bool hset(const std::string& key, const std::string& field, const std::string& value);
    bool hget(const std::string& key, const std::string& field, std::string& value);
    bool hgetall(const std::string& key, std::map<std::string, std::string>& out);

    // ---- 业务 key 便捷封装 ----
    std::string sessionKey(const std::string& token) const { return "session:" + token; }
    std::string onlineKey(int64_t uid) const { return "online:" + std::to_string(uid); }
    std::string roomKey(int64_t roomId) const { return "room:" + std::to_string(roomId); }
    std::string loginFailKey(const std::string& who) const { return "login_fail:" + who; }

private:
    RedisReply command(const std::vector<std::string>& args);
    bool ensureConnected();
    redisContext* ctx_ = nullptr;
    std::mutex* mtx_ = nullptr;            // hiredis 上下文非线程安全，串行化访问
};

} // namespace AIWZQ
