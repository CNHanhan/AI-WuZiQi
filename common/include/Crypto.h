#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace AIWZQ {
namespace Crypto {

// ---------- 编码 ----------
std::string base64Encode(const std::vector<uint8_t>& in);
std::vector<uint8_t> base64Decode(const std::string& in);

// ---------- 随机数 ----------
std::string randomBytes(size_t n);                       // 返回原始字节（可能含 \0）
std::string randomHex(size_t n);                         // 返回 2n 长度 hex 字符串（用于 nonce）

// ---------- 摘要 / MAC ----------
std::vector<uint8_t> sha256(const std::vector<uint8_t>& data);
std::vector<uint8_t> hmacSha256(const std::vector<uint8_t>& key,
                                const std::vector<uint8_t>& data);

// ---------- 对称加密 AES-256-GCM ----------
// key 必须 32 字节，iv 建议 12 字节；成功返回 true
bool aesGcmEncrypt(const std::vector<uint8_t>& plain, const std::vector<uint8_t>& key,
                   const std::vector<uint8_t>& iv,
                   std::vector<uint8_t>& cipher, std::vector<uint8_t>& tag);
bool aesGcmDecrypt(const std::vector<uint8_t>& cipher, const std::vector<uint8_t>& key,
                   const std::vector<uint8_t>& iv, const std::vector<uint8_t>& tag,
                   std::vector<uint8_t>& plain);

// ---------- 密码存储 Argon2id ----------
std::string hashPassword(const std::string& password);          // 返回 PHC 编码串
bool verifyPassword(const std::string& password, const std::string& encodedHash);

// ---------- ECDSA P-256 签名 ----------
std::vector<uint8_t> ecdsaSign(const std::vector<uint8_t>& data, const std::string& privPem);
bool ecdsaVerify(const std::vector<uint8_t>& data, const std::vector<uint8_t>& sig,
                 const std::string& pubPem);

// ---------- ECDH P-256 密钥交换 ----------
struct KeyPair { std::string privPem; std::string pubPem; };
KeyPair generateEcKeyPair();
std::vector<uint8_t> ecdhSharedSecret(const std::string& privPem, const std::string& peerPubPem);

} // namespace Crypto
} // namespace AIWZQ
