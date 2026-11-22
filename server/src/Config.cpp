/**
 * Config.cpp — INI 解析实现
 * 对应设计文档：01-服务端设计.md 3.2；06-加密系统设计.md 3.1
 */
#include "Config.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace AIWZQ {

namespace {

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) return "";
    const size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

// 去掉行内注释：'#' 或 ';' 之后的内容。保留值中未被引号包裹的 '#' 之前的空格处理交给 trim。
std::string stripComment(const std::string& s) {
    const size_t p = s.find_first_of("#;");
    return p == std::string::npos ? s : s.substr(0, p);
}

// 解析 ${ENV_VAR} 占位符；若环境变量存在（含空串）则返回其值，否则返回原串。
std::string expandEnv(const std::string& s) {
    if (s.size() >= 4 && s.compare(0, 2, "${") == 0 && s.back() == '}') {
        const std::string key = s.substr(2, s.size() - 3);
        const char* v = std::getenv(key.c_str());
        if (v) return std::string(v);
    }
    return s;
}

int toInt(const std::string& s, int fallback) {
    if (s.empty()) return fallback;
    try {
        size_t used = 0;
        const long long v = std::stoll(s, &used);
        if (used == s.size()) return static_cast<int>(v);
    } catch (...) {}
    return fallback;
}

} // namespace

Config& Config::instance() {
    static Config inst;
    return inst;
}

bool Config::load(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string section;
    std::string line;
    while (std::getline(in, line)) {
        const std::string cleaned = trim(stripComment(line));
        if (cleaned.empty()) continue;
        if (cleaned.front() == '[' && cleaned.back() == ']') {
            section = trim(cleaned.substr(1, cleaned.size() - 2));
            continue;
        }
        const size_t eq = cleaned.find('=');
        if (eq == std::string::npos) continue;
        const std::string key   = trim(cleaned.substr(0, eq));
        const std::string raw   = expandEnv(trim(cleaned.substr(eq + 1)));

        if (section == "server") {
            if      (key == "listen_port")     server_.listenPort     = toInt(raw, server_.listenPort);
            else if (key == "ssl_cert")        server_.sslCert        = raw;
            else if (key == "ssl_key")         server_.sslKey         = raw;
            else if (key == "max_connections") server_.maxConnections = toInt(raw, server_.maxConnections);
        } else if (section == "database") {
            if      (key == "host")     db_.host     = raw;
            else if (key == "port")     db_.port     = toInt(raw, db_.port);
            else if (key == "user")     db_.user     = raw;
            else if (key == "password") db_.password = raw;
            else if (key == "dbname")   db_.dbname   = raw;
        } else if (section == "redis") {
            if      (key == "host")     redis_.host     = raw;
            else if (key == "port")     redis_.port     = toInt(raw, redis_.port);
            else if (key == "password") redis_.password = raw;
            else if (key == "db")       redis_.db       = toInt(raw, redis_.db);
        } else if (section == "game") {
            if      (key == "total_time_sec") game_.totalTimeSec = toInt(raw, game_.totalTimeSec);
            else if (key == "step_time_sec")  game_.stepTimeSec  = toInt(raw, game_.stepTimeSec);
        } else if (section == "security") {
            if      (key == "login_max_attempts")   security_.loginMaxAttempts   = toInt(raw, security_.loginMaxAttempts);
            else if (key == "lockout_duration_min") security_.lockoutDurationMin = toInt(raw, security_.lockoutDurationMin);
            else if (key == "api_rate_limit")       security_.apiRateLimit       = toInt(raw, security_.apiRateLimit);
        }
    }

    // 密码兜底：即使 INI 写成明文，也以环境变量为准（06 文档 3.1）。
    if (const char* p = std::getenv("AI_WUZIQI_DB_PASSWORD")) db_.password = p;
    if (const char* p = std::getenv("AI_WUZIQI_REDIS_PASSWORD")) redis_.password = p;
    return true;
}

} // namespace AIWZQ
