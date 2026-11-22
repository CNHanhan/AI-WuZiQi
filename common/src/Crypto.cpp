// ============================================================================
// Crypto.cpp — 公共加密基础实现（OpenSSL EVP + Argon2id）
// 对应设计文档：06-加密系统设计.md
//   * 二、2.2 应用层 AES-256-GCM / ECDH 密钥交换（共享密钥 → HKDF → 会话密钥）
//   * 二、2.3 数字签名 ECDSA P-256（EVP_DigestSign/Verify + SHA256 + PEM）
//   * 二、2.4 密码存储 Argon2id（t=3, m=1<<16, p=4, len=32）
//   * 二、2.5 数据完整性 HMAC-SHA256
//   * 三、3.2 防重放 nonce + timestamp（randomHex 生成 nonce）
// 说明：Windows 下 OpenSSL/argon2 头文件包含路径由调用方 CMake 提供，
//       本文件使用标准 <openssl/...> 与 <argon2.h> 包含。
// ============================================================================

#include "Crypto.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/err.h>

#include <argon2.h>

#include <climits>
#include <string>
#include <vector>

namespace AIWZQ {
namespace Crypto {
namespace {

// 空 vector 的 data() 在 C++11 可能返回 nullptr，统一给一个安全只读缓冲。
const unsigned char* safeData(const std::vector<uint8_t>& v) {
    return v.empty() ? reinterpret_cast<const unsigned char*>("") : v.data();
}

EVP_PKEY* loadPrivateKey(const std::string& pem) {
    if (pem.empty()) return NULL;
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    if (!bio) return NULL;
    EVP_PKEY* key = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
    BIO_free(bio);
    return key;
}

EVP_PKEY* loadPublicKey(const std::string& pem) {
    if (pem.empty()) return NULL;
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    if (!bio) return NULL;
    EVP_PKEY* key = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    BIO_free(bio);
    return key;
}

// RFC 5869 HKDF-SHA256：把 ECDH 原始共享字节派生为 32 字节会话密钥。
std::vector<uint8_t> hkdfSha256(const std::vector<uint8_t>& ikm,
                                const std::vector<uint8_t>& salt,
                                const std::string& info,
                                size_t outLen) {
    std::vector<uint8_t> prk;
    if (salt.empty()) {
        const std::vector<uint8_t> zeroSalt(32, 0);
        prk = hmacSha256(zeroSalt, ikm);
    } else {
        prk = hmacSha256(salt, ikm);
    }

    std::vector<uint8_t> out;
    std::vector<uint8_t> prev;
    uint8_t counter = 1;
    while (out.size() < outLen) {
        std::vector<uint8_t> block = prev;
        block.insert(block.end(), info.begin(), info.end());
        block.push_back(counter);
        prev = hmacSha256(prk, block);
        out.insert(out.end(), prev.begin(), prev.end());
        ++counter;
    }
    out.resize(outLen);
    return out;
}

} // anonymous namespace

// ---------- 编码 ----------
std::string base64Encode(const std::vector<uint8_t>& in) {
    if (in.empty()) return std::string();
    const int len = EVP_EncodeBlock(NULL, safeData(in), static_cast<int>(in.size()));
    if (len < 0) return std::string();
    std::string out(static_cast<size_t>(len), '\0');
    EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&out[0]),
                    safeData(in), static_cast<int>(in.size()));
    return out;
}

std::vector<uint8_t> base64Decode(const std::string& in) {
    if (in.empty()) return std::vector<uint8_t>();
    std::vector<uint8_t> out(in.size() / 4 * 3 + 4, 0);
    const int len = EVP_DecodeBlock(out.data(),
                                    reinterpret_cast<const unsigned char*>(in.data()),
                                    static_cast<int>(in.size()));
    if (len < 0) return std::vector<uint8_t>();
    size_t real = static_cast<size_t>(len);
    // EVP_DecodeBlock 把 '=' 也计入输出，按填充位回退真实长度。
    if (real > 0 && in[in.size() - 1] == '=') --real;
    if (real > 0 && in.size() >= 2 && in[in.size() - 2] == '=') --real;
    out.resize(real);
    return out;
}

// ---------- 随机数 ----------
std::string randomBytes(size_t n) {
    if (n == 0) return std::string();
    if (n > static_cast<size_t>(INT_MAX)) return std::string();
    std::string out(n, '\0');
    if (RAND_bytes(reinterpret_cast<unsigned char*>(&out[0]), static_cast<int>(n)) != 1) {
        return std::string();
    }
    return out;
}

std::string randomHex(size_t n) {
    static const char kHex[] = "0123456789abcdef";
    const std::string raw = randomBytes(n);
    if (raw.empty() && n != 0) return std::string();
    std::string out;
    out.reserve(n * 2);
    for (size_t i = 0; i < raw.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(raw[i]);
        out.push_back(kHex[c >> 4]);
        out.push_back(kHex[c & 0x0F]);
    }
    return out;
}

// ---------- 摘要 / MAC ----------
std::vector<uint8_t> sha256(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out(EVP_MAX_MD_SIZE, 0);
    unsigned int len = 0;
    EVP_Digest(safeData(data), data.size(), out.data(), &len, EVP_sha256(), NULL);
    out.resize(len);
    return out;
}

std::vector<uint8_t> hmacSha256(const std::vector<uint8_t>& key,
                                const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out(EVP_MAX_MD_SIZE, 0);
    unsigned int len = 0;
    HMAC(EVP_sha256(), safeData(key), static_cast<int>(key.size()),
         safeData(data), data.size(), out.data(), &len);
    out.resize(len);
    return out;
}

// ---------- 对称加密 AES-256-GCM ----------
bool aesGcmEncrypt(const std::vector<uint8_t>& plain, const std::vector<uint8_t>& key,
                   const std::vector<uint8_t>& iv,
                   std::vector<uint8_t>& cipher, std::vector<uint8_t>& tag) {
    if (key.size() != 32) return false;      // AES-256 要求 32 字节密钥
    if (iv.empty()) return false;
    if (iv.size() > static_cast<size_t>(INT_MAX)) return false;
    if (plain.size() > static_cast<size_t>(INT_MAX)) return false;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;

    bool ok = false;
    do {
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN,
                                static_cast<int>(iv.size()), NULL) != 1) break;
        if (EVP_EncryptInit_ex(ctx, NULL, NULL, key.data(), iv.data()) != 1) break;

        cipher.assign(plain.size() + static_cast<size_t>(EVP_MAX_BLOCK_LENGTH), 0);
        int outLen = 0;
        int total = 0;
        if (!plain.empty()) {
            if (EVP_EncryptUpdate(ctx, cipher.data(), &outLen,
                                  plain.data(), static_cast<int>(plain.size())) != 1) break;
            total = outLen;
        }
        if (EVP_EncryptFinal_ex(ctx, cipher.data() + total, &outLen) != 1) break;
        total += outLen;
        cipher.resize(static_cast<size_t>(total));

        tag.assign(16, 0);
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag.data()) != 1) break;
        ok = true;
    } while (false);

    EVP_CIPHER_CTX_free(ctx);
    if (!ok) { cipher.clear(); tag.clear(); }
    return ok;
}

bool aesGcmDecrypt(const std::vector<uint8_t>& cipher, const std::vector<uint8_t>& key,
                   const std::vector<uint8_t>& iv, const std::vector<uint8_t>& tag,
                   std::vector<uint8_t>& plain) {
    if (key.size() != 32) return false;
    if (iv.empty()) return false;
    if (tag.size() != 16) return false;      // GCM 认证标签固定 16 字节
    if (cipher.size() > static_cast<size_t>(INT_MAX)) return false;
    if (iv.size() > static_cast<size_t>(INT_MAX)) return false;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;

    bool ok = false;
    do {
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN,
                                static_cast<int>(iv.size()), NULL) != 1) break;
        if (EVP_DecryptInit_ex(ctx, NULL, NULL, key.data(), iv.data()) != 1) break;

        plain.assign(cipher.size() + static_cast<size_t>(EVP_MAX_BLOCK_LENGTH), 0);
        int outLen = 0;
        int total = 0;
        if (!cipher.empty()) {
            if (EVP_DecryptUpdate(ctx, plain.data(), &outLen,
                                  cipher.data(), static_cast<int>(cipher.size())) != 1) break;
            total = outLen;
        }
        // OpenSSL 3 的 ctrl 需要可写指针，故去掉 tag 的 const。
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16,
                                const_cast<uint8_t*>(tag.data())) != 1) break;
        // Final 校验 tag；数据被篡改时返回 <= 0。
        if (EVP_DecryptFinal_ex(ctx, plain.data() + total, &outLen) != 1) break;
        total += outLen;
        plain.resize(static_cast<size_t>(total));
        ok = true;
    } while (false);

    EVP_CIPHER_CTX_free(ctx);
    if (!ok) plain.clear();
    return ok;
}

// ---------- 密码存储 Argon2id ----------
std::string hashPassword(const std::string& password) {
    uint8_t salt[16];
    if (RAND_bytes(salt, sizeof(salt)) != 1) return std::string();

    char encoded[256];
    const int rc = argon2id_hash_encoded(
        3,              // t：迭代次数
        1 << 16,        // m：内存费用 65536 KB = 64 MB
        4,              // p：并行度
        password.data(), password.size(),
        salt, sizeof(salt),
        32,             // hashlen：输出 32 字节
        encoded, sizeof(encoded));
    if (rc != ARGON2_OK) return std::string();
    return std::string(encoded);
}

bool verifyPassword(const std::string& password, const std::string& encodedHash) {
    if (encodedHash.empty()) return false;
    return argon2id_verify(encodedHash.c_str(), password.data(), password.size()) == ARGON2_OK;
}

// ---------- ECDSA P-256 签名 ----------
std::vector<uint8_t> ecdsaSign(const std::vector<uint8_t>& data, const std::string& privPem) {
    std::vector<uint8_t> sig;
    EVP_PKEY* key = loadPrivateKey(privPem);
    if (!key) return sig;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    do {
        if (!ctx) break;
        if (EVP_DigestSignInit(ctx, NULL, EVP_sha256(), NULL, key) != 1) break;
        if (EVP_DigestSignUpdate(ctx, safeData(data), data.size()) != 1) break;
        size_t sigLen = 0;
        if (EVP_DigestSignFinal(ctx, NULL, &sigLen) != 1) break;
        sig.resize(sigLen);
        if (EVP_DigestSignFinal(ctx, sig.data(), &sigLen) != 1) { sig.clear(); break; }
        sig.resize(sigLen);
    } while (false);

    if (ctx) EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(key);
    return sig;
}

bool ecdsaVerify(const std::vector<uint8_t>& data, const std::vector<uint8_t>& sig,
                 const std::string& pubPem) {
    if (sig.empty()) return false;
    EVP_PKEY* key = loadPublicKey(pubPem);
    if (!key) return false;

    bool ok = false;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    do {
        if (!ctx) break;
        if (EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, key) != 1) break;
        if (EVP_DigestVerifyUpdate(ctx, safeData(data), data.size()) != 1) break;
        ok = (EVP_DigestVerifyFinal(ctx, sig.data(), sig.size()) == 1);
    } while (false);

    if (ctx) EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(key);
    return ok;
}

// ---------- ECDH P-256 密钥交换 ----------
KeyPair generateEcKeyPair() {
    KeyPair kp;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
    if (!ctx) return kp;

    EVP_PKEY* pkey = NULL;
    do {
        if (EVP_PKEY_keygen_init(ctx) <= 0) break;
        if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1) <= 0) break;
        if (EVP_PKEY_keygen(ctx, &pkey) <= 0) break;

        BIO* privBio = BIO_new(BIO_s_mem());
        if (privBio) {
            if (PEM_write_bio_PrivateKey(privBio, pkey, NULL, NULL, 0, NULL, NULL) > 0) {
                char* mem = NULL;
                const long len = BIO_get_mem_data(privBio, &mem);
                if (len > 0 && mem) kp.privPem.assign(mem, static_cast<size_t>(len));
            }
            BIO_free(privBio);
        }

        BIO* pubBio = BIO_new(BIO_s_mem());
        if (pubBio) {
            if (PEM_write_bio_PUBKEY(pubBio, pkey) > 0) {
                char* mem = NULL;
                const long len = BIO_get_mem_data(pubBio, &mem);
                if (len > 0 && mem) kp.pubPem.assign(mem, static_cast<size_t>(len));
            }
            BIO_free(pubBio);
        }
    } while (false);

    if (pkey) EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(ctx);
    return kp;
}

std::vector<uint8_t> ecdhSharedSecret(const std::string& privPem,
                                      const std::string& peerPubPem) {
    std::vector<uint8_t> result;
    EVP_PKEY* priv = loadPrivateKey(privPem);
    EVP_PKEY* peer = loadPublicKey(peerPubPem);
    if (!priv || !peer) {
        if (priv) EVP_PKEY_free(priv);
        if (peer) EVP_PKEY_free(peer);
        return result;
    }

    // EVP_PKEY_derive 是 ECDH_compute_key 的现代等价接口，直接对
    // PKCS#8 私钥 / SubjectPublicKeyInfo 公钥操作，兼容 OpenSSL 1.1 与 3.x。
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(priv, NULL);
    do {
        if (!ctx) break;
        if (EVP_PKEY_derive_init(ctx) <= 0) break;
        if (EVP_PKEY_derive_set_peer(ctx, peer) <= 0) break;

        size_t secretLen = 0;
        if (EVP_PKEY_derive(ctx, NULL, &secretLen) <= 0) break;
        std::vector<uint8_t> secret(secretLen);
        if (EVP_PKEY_derive(ctx, secret.data(), &secretLen) <= 0) break;
        secret.resize(secretLen);

        // 06-加密系统设计.md 2.2：共享密钥 → HKDF → AES-256 会话密钥（32 字节）
        result = hkdfSha256(secret, std::vector<uint8_t>(), "AIWZQ-ECDH-P256", 32);
    } while (false);

    if (ctx) EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(priv);
    EVP_PKEY_free(peer);
    return result;
}

} // namespace Crypto
} // namespace AIWZQ
