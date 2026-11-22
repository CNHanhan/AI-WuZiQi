/**
 * SessionManager.cpp — 会话与登录失败锁定实现
 * 对应设计文档：01-服务端设计.md 2.8.4；04-数据库设计.md 3.7；06-加密系统设计.md 3.2
 */
#include "SessionManager.h"
#include "Config.h"
#include "Crypto.h"

namespace AIWZQ {

SessionManager::SessionManager(RedisClient& redis)
    : redis_(redis), maxAttempts_(Config::instance().security().loginMaxAttempts) {
    const int min = Config::instance().security().lockoutDurationMin;
    if (min > 0) lockoutSeconds_ = min * 60;
}

std::string SessionManager::createSession(int64_t uid, const std::string& ip) {
    const std::string token = Crypto::randomHex(32);   // 64 字符 hex
    redis_.set(redis_.sessionKey(token), std::to_string(uid), SESSION_EXPIRE_SECONDS);
    // 会话里附带 IP 便于审计：写入 user:{uid} hash（不覆盖已有字段）
    redis_.hset("user:" + std::to_string(uid), "last_ip", ip);
    // 在线状态：TTL 90s，心跳续期（04 文档 3.6）
    redis_.set(redis_.onlineKey(uid), "1", 90);
    return token;
}

bool SessionManager::validateSession(const std::string& token, int64_t& uid) {
    if (token.empty()) return false;
    std::string v;
    if (!redis_.get(redis_.sessionKey(token), v)) return false;
    try {
        uid = std::stoll(v);
    } catch (...) {
        return false;
    }
    if (uid <= 0) return false;
    // 滑动续期
    redis_.expire(redis_.sessionKey(token), SESSION_EXPIRE_SECONDS);
    redis_.set(redis_.onlineKey(uid), "1", 90);
    return true;
}

void SessionManager::destroySession(const std::string& token) {
    int64_t uid = 0;
    if (validateSession(token, uid) && uid > 0)
        redis_.del(redis_.onlineKey(uid));
    redis_.del(redis_.sessionKey(token));
}

bool SessionManager::checkOne(const std::string& who) {
    const std::string key = redis_.loginFailKey(who);
    std::string v;
    int64_t count = 0;
    if (redis_.get(key, v)) {
        try { count = std::stoll(v); } catch (...) { count = 0; }
    }
    if (count >= maxAttempts_) return false;   // 仍在锁定期（key 未过期即锁定）
    redis_.incr(key, count);
    // 首次失败时设置 15 分钟过期（INCR 不会清除 TTL，仅首次无 TTL 时设置）
    if (count == 1) redis_.expire(key, lockoutSeconds_);
    return count < maxAttempts_;
}

bool SessionManager::checkLoginAttempt(const std::string& ip, const std::string& username) {
    const bool ipOk = checkOne("ip:" + ip);
    const bool userOk = checkOne("user:" + username);
    return ipOk && userOk;
}

void SessionManager::clearLoginAttempts(const std::string& ip, const std::string& username) {
    redis_.del(redis_.loginFailKey("ip:" + ip));
    redis_.del(redis_.loginFailKey("user:" + username));
}

int SessionManager::lockoutSecondsLeft(const std::string& ip, const std::string& username) {
    // hiredis 没有 TTL 封装在 RedisClient 里，用 EXISTS 判断即可：
    // 任一 key 存在且次数达上限则视为锁定，返回 900 的近似值。
    auto countOf = [&](const std::string& who) -> int64_t {
        std::string v;
        if (!redis_.get(redis_.loginFailKey(who), v)) return 0;
        try { return std::stoll(v); } catch (...) { return 0; }
    };
    int64_t a = countOf("ip:" + ip), b = countOf("user:" + username);
    return (a >= maxAttempts_ || b >= maxAttempts_) ? lockoutSeconds_ : 0;
}

} // namespace AIWZQ
