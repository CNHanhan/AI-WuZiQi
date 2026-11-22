#pragma once
/**
 * RateLimiter.h — 基于 Redis 的频率限制
 * 对应设计文档：01-服务端设计.md 2.8.2（API 60/min、聊天 1/s、登录失败 5 次锁 15 分钟）
 *               06-加密系统设计.md 3.2（防 DDoS / 防暴力破解）
 *
 * 限流 key：
 *   rate:api:{ip}     INCR，TTL 60s，>limit 拒绝
 *   rate:chat:{uid}   INCR，TTL 1s，>1 拒绝
 *   登录失败锁定委托 SessionManager::checkLoginAttempt
 */
#include "RedisClient.h"
#include "SessionManager.h"

#include <cstdint>
#include <string>

namespace AIWZQ {

class RateLimiter {
public:
    RateLimiter(RedisClient& redis, SessionManager& sessions);

    // API 调用频率限制：每 ip 每分钟最多 60 次；true=放行
    bool checkApiRate(const std::string& ip);

    // 聊天频率限制：每 uid 每秒最多 1 条；true=放行
    bool checkChatRate(int64_t uid);

    // 登录失败锁定：委托 SessionManager（5 次 / 15 分钟）；true=允许尝试
    bool checkLoginAttempt(const std::string& ip, const std::string& username);

    // 登录成功后清零
    void clearLoginAttempts(const std::string& ip, const std::string& username);

    // 自定义配置覆盖
    void setApiLimit(int perMinute) { apiLimit_ = perMinute; }

private:
    RedisClient& redis_;
    SessionManager& sessions_;
    int apiLimit_ = 60;
};

} // namespace AIWZQ
