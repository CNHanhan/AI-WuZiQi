// AI-WuZiQi 服务端入口
// 读 server.conf（INI，Config 单例负责解析与 ${ENV_VAR} 展开）、初始化 spdlog，
// 通过网络层工厂函数 createBizHandler() 装配业务层，然后阻塞运行 Reactor。

#include "TcpServer.h"
#include "Config.h"
#include "IMessageHandler.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <cstdlib>
#include <string>
#include <vector>

// biz 模块提供；仅在装配层前置声明，网络层库对 biz 零耦合。
extern "C" AIWZQ::IMessageHandler* createBizHandler();

namespace {

void initLogger() {
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        "aiwzq_server.log", 10 * 1024 * 1024, 3);
    std::vector<spdlog::sink_ptr> sinks{console, file};
    auto logger = std::make_shared<spdlog::logger>("aiwzq", sinks.begin(), sinks.end());
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
}

} // namespace

int main(int argc, char** argv) {
    initLogger();

    const std::string confPath = (argc > 1) ? argv[1] : "server.conf";
    AIWZQ::Config& cfg = AIWZQ::Config::instance();
    if (!cfg.load(confPath)) {
        spdlog::warn("config {} not found, using defaults", confPath);
    }

    int  port     = cfg.server().listenPort;
    const std::string cert = cfg.server().sslCert;
    const std::string key  = cfg.server().sslKey;
    const bool wantTls = !cert.empty() && !key.empty();
    if (wantTls && port == 8888) port = 8889;   // 协议约定 SSL 端口 8889

    const char* dbPass = std::getenv("AI_WUZIQI_DB_PASSWORD");
    if (!dbPass) spdlog::warn("AI_WUZIQI_DB_PASSWORD not set; DB auth may fail.");

    spdlog::info("AI-WuZiQi Server starting: conf={} port={} tls={} max_conn={} db={}",
                 confPath, port, wantTls, cfg.server().maxConnections,
                 cfg.database().dbname);

    // 业务实例由 biz 工厂提供（唯一契约）
    AIWZQ::IMessageHandler* biz = createBizHandler();
    if (!biz) {
        spdlog::error("createBizHandler returned nullptr");
        return 1;
    }

    AIWZQ::TcpServer server(port, 4);
    server.setHandler(biz);

    if (wantTls && !server.enableTls(cert, key)) {
        spdlog::warn("TLS setup failed, running in plaintext mode");
    }

    // TcpServer 内置 timer 线程每秒触发 onTimer_（扩展点）。
    // biz 的超时扫描由 BizHandler 自带幂等 tick 线程完成，故此处无需注入逻辑；
    // 如需改为网络层驱动，仅需：server.setOnTimer([biz]{ ... tick(); });
    server.start();
    server.run();
    return 0;
}
