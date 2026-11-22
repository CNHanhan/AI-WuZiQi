/**
 * SecurityLogger.cpp — 审计日志写入 t_security_log
 * 对应设计文档：01-服务端设计.md 2.8.3；04-数据库设计.md 3.8；06-加密系统设计.md 3.3
 */
#include "SecurityLogger.h"

namespace AIWZQ {

SecurityLogger::SecurityLogger(Database& db) : db_(db) {}

void SecurityLogger::write_(int64_t uid, const std::string& ip,
                            const std::string& action, int status,
                            const std::string& details) {
    db_.insertSecurityLog(uid, ip, action, status, details);
}

void SecurityLogger::logLogin(const std::string& ip, const std::string& username,
                              bool success, const std::string& reason) {
    write_(0, ip, "login", success ? 1 : 0,
           "user=" + username + "; " + reason);
}

void SecurityLogger::logSensitiveOperation(int64_t uid, const std::string& operation,
                                           const std::string& details) {
    write_(uid, "", operation, 1, details);
}

void SecurityLogger::logSuspiciousActivity(const std::string& ip, const std::string& activity) {
    write_(0, ip, "suspicious", 0, activity);
}

void SecurityLogger::logSuspiciousActivity(int64_t uid, const std::string& ip,
                                           const std::string& activity) {
    write_(uid, ip, "suspicious", 0, activity);
}

} // namespace AIWZQ
