/**
 * RateLimiter.cpp — Redis 计数窗口限流实现
 * 对应设计文档：01-服务端设计.md 2.8.2；06-加密系统设计.md 3.2
 */
#include "RateLimiter.h"
#include "Config.h"

namespace AIWZQ {

RateLimiter::RateLimiter(RedisClient& redis, SessionManager& sessions)
    : redis_(redis), sessions_(sessions) {
    apiLimit_ = Config::instance().security().apiRateLimit;
}

bool RateLimiter::checkApiRate(const std::string& ip) {
    const std::string key = "rate:api:" + ip;
    int64_t count = 0;
    if (!redis_.incr(key, count)) return true;      // Redis 故障时放行，避免全站不可用
    if (count == 1) redis_.expire(key, 60);
    return count <= apiLimit_;
}

bool RateLimiter::checkChatRate(int64_t uid) {
    const std::string key = "rate:chat:" + std::to_string(uid);
    int64_t count = 0;
    if (!redis_.incr(key, count)) return true;
    if (count == 1) redis_.expire(key, 1);
    return count <= 1;
}

bool RateLimiter::checkLoginAttempt(const std::string& ip, const std::string& username) {
    return sessions_.checkLoginAttempt(ip, username);
}

void RateLimiter::clearLoginAttempts(const std::string& ip, const std::string& username) {
    sessions_.clearLoginAttempts(ip, username);
}

} // namespace AIWZQ
