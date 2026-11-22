#pragma once
/**
 * SessionManager.h — 会话与登录失败锁定
 * 对应设计文档：01-服务端设计.md 2.8.4（SessionManager, TTL=7200）
 *               04-数据库设计.md 3.7（session:{token}, login_fail:{ip|username}）
 *               06-加密系统设计.md 2.4/3.2（Crypto::randomHex 生成 token，5 次锁 15 分钟）
 */
#include "RedisClient.h"

#include <cstdint>
#include <string>

namespace AIWZQ {

class SessionManager {
public:
    explicit SessionManager(RedisClient& redis);

    // 登录成功后创建会话：token = Crypto::randomHex(32)，存 session:{token} TTL 7200
    std::string createSession(int64_t uid, const std::string& ip);

    // 校验 token：成功写入 uid 并续期；失败返回 false
    bool validateSession(const std::string& token, int64_t& uid);

    // 登出：删除 session key 与 online key
    void destroySession(const std::string& token);

    // 登录失败计数：返回 true 表示允许继续尝试，false 表示锁定（5 次 / 15 分钟）
    // ip 与 username 两个维度都计数，任一达上限即锁定。
    bool checkLoginAttempt(const std::string& ip, const std::string& username);

    // 登录成功：清零失败计数
    void clearLoginAttempts(const std::string& ip, const std::string& username);

    // 查询剩余锁定秒数（0 表示未锁定）
    int lockoutSecondsLeft(const std::string& ip, const std::string& username);

    static constexpr int SESSION_EXPIRE_SECONDS = 7200;
    static constexpr int DEFAULT_LOCKOUT_SECONDS = 900;   // 15 分钟

private:
    RedisClient& redis_;
    int maxAttempts_ = 5;
    int lockoutSeconds_ = DEFAULT_LOCKOUT_SECONDS;        // 由 security.lockout_duration_min 计算

    bool checkOne(const std::string& who);
};

} // namespace AIWZQ
