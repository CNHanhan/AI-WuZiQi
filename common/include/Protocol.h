#pragma once
#include "Types.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>

namespace AIWZQ {

using json = nlohmann::json;

// 单帧最大长度（防止内存耗尽攻击），1 MiB
constexpr uint32_t MAX_FRAME_BYTES = 1024u * 1024u;

// ---------- 通用消息 ----------
struct Message {
    std::string type;
    int64_t     seq = 0;
    std::string nonce;
    int64_t     timestamp = 0;
    json        data = json::object();
};

class DecodeError : public std::runtime_error {
public:
    explicit DecodeError(const std::string& what) : std::runtime_error(what) {}
};

// ---------- 帧格式：[4字节大端长度][JSON Payload] ----------
inline std::string encodeFrame(const Message& m) {
    json j;
    j["type"]      = m.type;
    j["seq"]       = m.seq;
    j["nonce"]     = m.nonce;
    j["timestamp"] = m.timestamp;
    j["data"]      = m.data;
    const std::string payload = j.dump();
    const uint32_t len = static_cast<uint32_t>(payload.size());
    std::string frame;
    frame.reserve(4 + payload.size());
    frame.push_back(static_cast<char>((len >> 24) & 0xFF));
    frame.push_back(static_cast<char>((len >> 16) & 0xFF));
    frame.push_back(static_cast<char>((len >> 8)  & 0xFF));
    frame.push_back(static_cast<char>( len        & 0xFF));
    frame.append(payload);
    return frame;
}

// 从缓冲区起始处尝试解析一帧。
// 返回 true 表示成功，out 被填充，frameLen 为消耗的字节数；
// 返回 false 表示数据不足（半包），调用方应继续接收；
// 超长或非法 JSON 抛 DecodeError。
inline bool decodeOneFrame(const char* buf, size_t bufLen, Message& out, size_t& frameLen) {
    if (bufLen < 4) return false;
    const uint32_t len =
        (static_cast<uint32_t>(static_cast<uint8_t>(buf[0])) << 24) |
        (static_cast<uint32_t>(static_cast<uint8_t>(buf[1])) << 16) |
        (static_cast<uint32_t>(static_cast<uint8_t>(buf[2])) << 8)  |
        (static_cast<uint32_t>(static_cast<uint8_t>(buf[3])));
    if (len == 0) throw DecodeError("empty frame payload");
    if (len > MAX_FRAME_BYTES) throw DecodeError("frame too large");
    const size_t total = 4u + static_cast<size_t>(len);
    if (bufLen < total) return false;

    const std::string payload(buf + 4, len);
    json j;
    try {
        j = json::parse(payload);
    } catch (const std::exception& e) {
        throw DecodeError(std::string("bad json: ") + e.what());
    }
    out.type = j.value("type", std::string());
    out.seq  = j.value("seq", static_cast<int64_t>(0));
    out.nonce= j.value("nonce", std::string());
    out.timestamp = j.value("timestamp", static_cast<int64_t>(0));
    out.data = j.contains("data") ? j["data"] : json::object();
    frameLen = total;
    return true;
}

// ---------- 粘包/拆包缓冲 ----------
class Buffer {
public:
    void append(const char* p, size_t n) { data_.append(p, n); }
    size_t size() const { return data_.size(); }
    bool empty() const { return data_.empty(); }
    const std::string& str() const { return data_; }

    // 弹出一帧；半包返回 false 且不消费数据
    bool popFrame(Message& out) {
        size_t used = 0;
        if (!decodeOneFrame(data_.data(), data_.size(), out, used)) return false;
        data_.erase(0, used);
        return true;
    }
private:
    std::string data_;
};

// ---------- 便捷构造 ----------
inline Message makeMessage(const std::string& type, int64_t seq, const json& data,
                           const std::string& nonce = std::string(), int64_t ts = 0) {
    Message m;
    m.type = type; m.seq = seq; m.data = data; m.nonce = nonce; m.timestamp = ts;
    return m;
}

} // namespace AIWZQ
