#pragma once
/**
 * SecurityLogger.h — 安全审计日志
 * 对应设计文档：01-服务端设计.md 2.8.3（logLogin/logSensitiveOperation/logSuspiciousActivity）
 *               04-数据库设计.md 3.8（t_security_log）
 *               06-加密系统设计.md 3.3（审计日志内容）
 */
#include "Database.h"

#include <cstdint>
#include <string>

namespace AIWZQ {

class SecurityLogger {
public:
    explicit SecurityLogger(Database& db);

    // 登录成功/失败（失败时 uid 传 0）
    void logLogin(const std::string& ip, const std::string& username,
                  bool success, const std::string& reason);

    // 敏感操作（密码修改、购买、装备等）
    void logSensitiveOperation(int64_t uid, const std::string& operation,
                               const std::string& details);

    // 异常行为（频繁请求、非法输入等），uid 传 0 表示未登录
    void logSuspiciousActivity(const std::string& ip, const std::string& activity);
    void logSuspiciousActivity(int64_t uid, const std::string& ip,
                               const std::string& activity);

private:
    void write_(int64_t uid, const std::string& ip, const std::string& action,
                int status, const std::string& details);
    Database& db_;
};

} // namespace AIWZQ
