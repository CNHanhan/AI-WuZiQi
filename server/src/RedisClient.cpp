/**
 * RedisClient.cpp — hiredis 命令实现
 * 对应设计文档：04-数据库设计.md 3.6/3.7；01-服务端设计.md 2.8
 */
#include "RedisClient.h"
#include "Config.h"

#include <hiredis/hiredis.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace AIWZQ {

namespace {

// 把 hiredis redisReply 转成结构化的 RedisReply（递归释放由 RAII 负责）
RedisReply convert(redisReply* r) {
    RedisReply out;
    if (!r) return out;
    switch (r->type) {
    case REDIS_REPLY_STATUS:
        out.ok = true;
        out.str = r->str ? std::string(r->str, r->len) : std::string("OK");
        break;
    case REDIS_REPLY_INTEGER:
        out.ok = true;
        out.integer = r->integer;
        break;
    case REDIS_REPLY_STRING:
        out.ok = true;
        out.str = r->str ? std::string(r->str, r->len) : std::string();
        break;
    case REDIS_REPLY_NIL:
        out.nil = true;
        break;
    case REDIS_REPLY_ARRAY:
        out.ok = true;
        if (r->elements > 0 && r->element[0] &&
            r->element[0]->type == REDIS_REPLY_STRING) {
            // hgetall 扁平数组：field/value 成对
            for (size_t i = 0; i + 1 < r->elements; i += 2) {
                redisReply* f = r->element[i];
                redisReply* v = r->element[i + 1];
                if (f && v && f->str && v->str)
                    out.hash.emplace(std::string(f->str, f->len), std::string(v->str, v->len));
            }
        }
        for (size_t i = 0; i < r->elements; ++i) {
            redisReply* e = r->element[i];
            if (e && e->type == REDIS_REPLY_STRING && e->str)
                out.array.emplace_back(e->str, e->len);
            else if (e && e->type == REDIS_REPLY_INTEGER)
                out.array.push_back(std::to_string(e->integer));
            else
                out.array.emplace_back();
        }
        break;
    case REDIS_REPLY_ERROR:
        out.ok = false;
        out.str = r->str ? std::string(r->str, r->len) : std::string("ERR");
        break;
    default:
        break;
    }
    return out;
}

} // namespace

RedisClient::RedisClient() : mtx_(new std::mutex()) {}

RedisClient::~RedisClient() {
    close();
    delete mtx_;
}

void RedisClient::close() {
    if (ctx_) {
        redisFree(ctx_);
        ctx_ = nullptr;
    }
}

bool RedisClient::connect() {
    close();
    const RedisConfig& cfg = Config::instance().redis();
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    ctx_ = redisConnectWithTimeout(cfg.host.c_str(), cfg.port, tv);
    if (!ctx_ || ctx_->err) { close(); return false; }
    if (!cfg.password.empty()) {
        redisReply* r = static_cast<redisReply*>(redisCommand(ctx_, "AUTH %s", cfg.password.c_str()));
        if (!r || r->type == REDIS_REPLY_ERROR) { if (r) freeReplyObject(r); close(); return false; }
        freeReplyObject(r);
    }
    if (cfg.db != 0) {
        redisReply* r = static_cast<redisReply*>(redisCommand(ctx_, "SELECT %d", cfg.db));
        if (!r || r->type == REDIS_REPLY_ERROR) { if (r) freeReplyObject(r); close(); return false; }
        freeReplyObject(r);
    }
    return true;
}

bool RedisClient::ensureConnected() {
    if (ctx_) return true;
    return connect();
}

RedisReply RedisClient::command(const std::vector<std::string>& args) {
    std::lock_guard<std::mutex> lk(*mtx_);
    if (!ensureConnected()) return RedisReply();

    // 构造 argv / argvlen，二进制安全
    std::vector<const char*> argv;
    std::vector<size_t> argvlen;
    argv.reserve(args.size());
    argvlen.reserve(args.size());
    for (const auto& a : args) {
        argv.push_back(a.data());
        argvlen.push_back(a.size());
    }
    redisReply* r = static_cast<redisReply*>(
        redisCommandArgv(ctx_, static_cast<int>(argv.size()), argv.data(), argvlen.data()));
    RedisReply out = convert(r);
    if (r) freeReplyObject(r);
    // 连接出错则丢弃，下次自动重连
    if (ctx_ && ctx_->err) close();
    return out;
}

bool RedisClient::get(const std::string& key, std::string& value) {
    RedisReply r = command({"GET", key});
    if (r.ok && !r.nil) { value = r.str; return true; }
    return false;
}

bool RedisClient::set(const std::string& key, const std::string& value, int ttlSeconds) {
    RedisReply r;
    if (ttlSeconds > 0)
        r = command({"SET", key, value, "EX", std::to_string(ttlSeconds)});
    else
        r = command({"SET", key, value});
    return r.ok;
}

bool RedisClient::del(const std::string& key) {
    return command({"DEL", key}).ok;
}

bool RedisClient::exists(const std::string& key) {
    RedisReply r = command({"EXISTS", key});
    return r.ok && r.integer > 0;
}

bool RedisClient::expire(const std::string& key, int ttlSeconds) {
    RedisReply r = command({"EXPIRE", key, std::to_string(ttlSeconds)});
    return r.ok && r.integer > 0;
}

bool RedisClient::incr(const std::string& key, int64_t& newValue) {
    RedisReply r = command({"INCR", key});
    if (r.ok) { newValue = r.integer; return true; }
    return false;
}

bool RedisClient::zadd(const std::string& key, double score, const std::string& member) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", score);
    RedisReply r = command({"ZADD", key, buf, member});
    return r.ok;
}

bool RedisClient::zscore(const std::string& key, const std::string& member, double& score) {
    RedisReply r = command({"ZSCORE", key, member});
    if (r.ok && !r.nil && !r.str.empty()) {
        try { score = std::stod(r.str); return true; } catch (...) { return false; }
    }
    return false;
}

bool RedisClient::zrange(const std::string& key, int start, int stop,
                         std::vector<std::string>& out, bool withScores) {
    std::vector<std::string> args = {"ZREVRANGE", key, std::to_string(start), std::to_string(stop)};
    if (withScores) args.push_back("WITHSCORES");
    RedisReply r = command(args);
    if (!r.ok) return false;
    out = r.array;
    return true;
}

bool RedisClient::hset(const std::string& key, const std::string& field, const std::string& value) {
    return command({"HSET", key, field, value}).ok;
}

bool RedisClient::hget(const std::string& key, const std::string& field, std::string& value) {
    RedisReply r = command({"HGET", key, field});
    if (r.ok && !r.nil) { value = r.str; return true; }
    return false;
}

bool RedisClient::hgetall(const std::string& key, std::map<std::string, std::string>& out) {
    RedisReply r = command({"HGETALL", key});
    if (!r.ok) return false;
    out = r.hash;
    return true;
}

} // namespace AIWZQ
