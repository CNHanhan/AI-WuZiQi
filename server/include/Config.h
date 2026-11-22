#pragma once
/**
 * Config.h — 服务端配置解析（INI）
 * 对应设计文档：01-服务端设计.md 3.2（server.conf 配置）
 *               06-加密系统设计.md 3.1（数据库/Redis 密码从环境变量读取，不写死）
 */
#include <cstdint>
#include <string>

namespace AIWZQ {

struct ServerConfig {
    int         listenPort    = 8888;
    std::string sslCert;
    std::string sslKey;
    int         maxConnections = 10000;
};

struct DbConfig {
    std::string host     = "127.0.0.1";
    int         port     = 3306;
    std::string user     = "wuziqi";
    std::string password;                 // 来自 AI_WUZIQI_DB_PASSWORD
    std::string dbname   = "ai_wuziqi";
};

struct RedisConfig {
    std::string host     = "127.0.0.1";
    int         port     = 6379;
    std::string password;                 // 来自 AI_WUZIQI_REDIS_PASSWORD
    int         db       = 0;
};

struct GameConfig {
    int totalTimeSec = 180;               // 每方总时限
    int stepTimeSec  = 30;                // 每步时限
};

struct SecurityConfig {
    int loginMaxAttempts   = 5;
    int lockoutDurationMin = 15;
    int apiRateLimit       = 60;
};

// 全局只读配置单例：服务启动时 load() 一次，之后各模块只读访问。
class Config {
public:
    static Config& instance();

    // 解析 INI 文件；文件不存在时返回 false 并保留默认值。
    bool load(const std::string& path = "server.conf");

    const ServerConfig&   server()   const { return server_; }
    const DbConfig&       database() const { return db_; }
    const RedisConfig&    redis()    const { return redis_; }
    const GameConfig&     game()     const { return game_; }
    const SecurityConfig& security() const { return security_; }

private:
    Config() = default;
    ServerConfig   server_;
    DbConfig       db_;
    RedisConfig    redis_;
    GameConfig     game_;
    SecurityConfig security_;
};

} // namespace AIWZQ
