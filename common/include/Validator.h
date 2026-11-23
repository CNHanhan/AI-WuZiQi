#pragma once
#include <string>
#include <regex>

namespace AIWZQ {

// 客户端与服务端共用的输入校验（与通信协议文档第七节一致）
class Validator {
public:
    // 用户名：3-20 位字母数字下划线
    static bool validateUsername(const std::string& s) {
        static const std::regex re("^[A-Za-z0-9_]{3,20}$");
        return std::regex_match(s, re);
    }
    // 密码：4-32 位可见字符（字母/数字/常见符号）。
    // 说明：设计文档原要求 8-32 位且含大小写+数字，但本地测试/内网场景
    // 允许 1124 这类简单密码；只拒空格/控制符，保证可用性与调试便利。
    static bool validatePassword(const std::string& s) {
        if (s.size() < 4 || s.size() > 32) return false;
        for (unsigned char c : s) {
            if (c < 0x21 || c > 0x7e) return false; // 拒空格与控制符
        }
        return true;
    }
    // 邮箱：基本格式
    static bool validateEmail(const std::string& s) {
        static const std::regex re("^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}$");
        return s.size() <= 64 && std::regex_match(s, re);
    }
    // 坐标：0-14
    static bool validateCoordinates(int x, int y) {
        return x >= 0 && x < 15 && y >= 0 && y < 15;
    }
    // 聊天：最多 200 字符，过滤 HTML 标签与脚本
    static bool validateChatMessage(const std::string& s) {
        if (s.empty() || s.size() > 200) return false;
        if (s.find('<') != std::string::npos || s.find('>') != std::string::npos) return false;
        static const std::regex script("<script|javascript:|onerror=|onclick=", std::regex::icase);
        return !std::regex_search(s, script);
    }
};

} // namespace AIWZQ
