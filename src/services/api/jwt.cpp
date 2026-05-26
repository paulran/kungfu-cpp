#include "jwt.h"
#include <nlohmann/json.hpp>
#include <cstring>
#include <ctime>
#include <array>
#include <vector>
#include <algorithm>

namespace kungfu::service::jwt {

namespace {

// Minimal HMAC-SHA256 implementation
// Based on RFC 2104 and FIPS 180-4

struct SHA256 {
    uint32_t state[8]{};
    uint64_t bitcount = 0;
    uint8_t buffer[64]{};

    SHA256() {
        state[0] = 0x6a09e667; state[1] = 0xbb67ae85;
        state[2] = 0x3c6ef372; state[3] = 0xa54ff53a;
        state[4] = 0x510e527f; state[5] = 0x9b05688c;
        state[6] = 0x1f83d9ab; state[7] = 0x5be0cd19;
    }

    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    static uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
    static uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
    static uint32_t sigma0(uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
    static uint32_t sigma1(uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
    static uint32_t gamma0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
    static uint32_t gamma1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

    void transform(const uint8_t* block) {
        static constexpr uint32_t K[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
        };

        uint32_t W[64];
        for (int i = 0; i < 16; i++) {
            W[i] = (uint32_t(block[i*4]) << 24) | (uint32_t(block[i*4+1]) << 16) |
                   (uint32_t(block[i*4+2]) << 8) | uint32_t(block[i*4+3]);
        }
        for (int i = 16; i < 64; i++) {
            W[i] = gamma1(W[i-2]) + W[i-7] + gamma0(W[i-15]) + W[i-16];
        }

        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

        for (int i = 0; i < 64; i++) {
            uint32_t t1 = h + sigma1(e) + ch(e, f, g) + K[i] + W[i];
            uint32_t t2 = sigma0(a) + maj(a, b, c);
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }

        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }

    void update(const uint8_t* data, size_t len) {
        size_t offset = static_cast<size_t>((bitcount / 8) % 64);
        bitcount += len * 8;

        for (size_t i = 0; i < len; i++) {
            buffer[offset++] = data[i];
            if (offset == 64) {
                transform(buffer);
                offset = 0;
            }
        }
    }

    std::array<uint8_t, 32> finalize() {
        size_t offset = static_cast<size_t>((bitcount / 8) % 64);
        buffer[offset++] = 0x80;

        if (offset > 56) {
            std::memset(buffer + offset, 0, 64 - offset);
            transform(buffer);
            offset = 0;
        }

        std::memset(buffer + offset, 0, 56 - offset);
        for (int i = 0; i < 8; i++) {
            buffer[56 + i] = static_cast<uint8_t>(bitcount >> (56 - i * 8));
        }
        transform(buffer);

        std::array<uint8_t, 32> digest{};
        for (int i = 0; i < 8; i++) {
            digest[i*4+0] = static_cast<uint8_t>(state[i] >> 24);
            digest[i*4+1] = static_cast<uint8_t>(state[i] >> 16);
            digest[i*4+2] = static_cast<uint8_t>(state[i] >> 8);
            digest[i*4+3] = static_cast<uint8_t>(state[i]);
        }
        return digest;
    }
};

std::array<uint8_t, 32> hmac_sha256(const std::string& key, const std::string& message) {
    std::array<uint8_t, 64> k_pad{};
    if (key.size() > 64) {
        SHA256 h;
        h.update(reinterpret_cast<const uint8_t*>(key.data()), key.size());
        auto hashed = h.finalize();
        std::memcpy(k_pad.data(), hashed.data(), 32);
    } else {
        std::memcpy(k_pad.data(), key.data(), key.size());
    }

    std::array<uint8_t, 64> i_pad{}, o_pad{};
    for (int i = 0; i < 64; i++) {
        i_pad[i] = k_pad[i] ^ 0x36;
        o_pad[i] = k_pad[i] ^ 0x5c;
    }

    SHA256 inner;
    inner.update(i_pad.data(), 64);
    inner.update(reinterpret_cast<const uint8_t*>(message.data()), message.size());
    auto inner_hash = inner.finalize();

    SHA256 outer;
    outer.update(o_pad.data(), 64);
    outer.update(inner_hash.data(), 32);
    return outer.finalize();
}

static const char base64url_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string base64url_encode(const uint8_t* data, size_t len) {
    std::string result;
    result.reserve((len * 4 + 2) / 3);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = uint32_t(data[i]) << 16;
        if (i + 1 < len) n |= uint32_t(data[i+1]) << 8;
        if (i + 2 < len) n |= uint32_t(data[i+2]);

        result += base64url_chars[(n >> 18) & 0x3F];
        result += base64url_chars[(n >> 12) & 0x3F];
        if (i + 1 < len) result += base64url_chars[(n >> 6) & 0x3F];
        if (i + 2 < len) result += base64url_chars[n & 0x3F];
    }
    return result;
}

std::string base64url_encode(const std::string& s) {
    return base64url_encode(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

std::string base64url_decode(const std::string& input) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '-') return 62;
        if (c == '_') return 63;
        return -1;
    };

    std::string result;
    result.reserve(input.size() * 3 / 4);

    for (size_t i = 0; i < input.size(); i += 4) {
        int n = 0;
        int pad = 0;
        for (int j = 0; j < 4; j++) {
            n <<= 6;
            if (i + j < input.size()) {
                int v = val(input[i+j]);
                if (v >= 0) n |= v;
                else pad++;
            } else {
                pad++;
            }
        }
        result += static_cast<char>((n >> 16) & 0xFF);
        if (pad < 2) result += static_cast<char>((n >> 8) & 0xFF);
        if (pad < 1) result += static_cast<char>(n & 0xFF);
    }
    return result;
}

} // anonymous namespace

std::string create_token(const std::string& secret,
                         const std::string& username,
                         int64_t expire_time_s) {
    nlohmann::json header = {{"alg", "HS256"}, {"typ", "JWT"}};
    nlohmann::json payload = {
        {"sub", username},
        {"exp", expire_time_s},
        {"iat", std::time(nullptr)}
    };

    std::string header_enc = base64url_encode(header.dump());
    std::string payload_enc = base64url_encode(payload.dump());
    std::string signing_input = header_enc + "." + payload_enc;

    auto sig = hmac_sha256(secret, signing_input);
    std::string sig_enc = base64url_encode(sig.data(), sig.size());

    return signing_input + "." + sig_enc;
}

bool verify_token(const std::string& secret, const std::string& token) {
    auto dot1 = token.find('.');
    if (dot1 == std::string::npos) return false;
    auto dot2 = token.find('.', dot1 + 1);
    if (dot2 == std::string::npos) return false;

    std::string signing_input = token.substr(0, dot2);
    std::string sig_provided = token.substr(dot2 + 1);

    auto expected_sig = hmac_sha256(secret, signing_input);
    std::string expected_enc = base64url_encode(expected_sig.data(), expected_sig.size());

    if (sig_provided != expected_enc) return false;

    // Check expiry
    int64_t exp = get_expiry(token);
    if (exp > 0 && std::time(nullptr) > exp) return false;

    return true;
}

std::string get_username(const std::string& token) {
    auto dot1 = token.find('.');
    if (dot1 == std::string::npos) return "";
    auto dot2 = token.find('.', dot1 + 1);
    if (dot2 == std::string::npos) return "";

    std::string payload_enc = token.substr(dot1 + 1, dot2 - dot1 - 1);
    std::string payload_json = base64url_decode(payload_enc);

    try {
        auto j = nlohmann::json::parse(payload_json);
        return j.value("sub", "");
    } catch (...) {
        return "";
    }
}

int64_t get_expiry(const std::string& token) {
    auto dot1 = token.find('.');
    if (dot1 == std::string::npos) return 0;
    auto dot2 = token.find('.', dot1 + 1);
    if (dot2 == std::string::npos) return 0;

    std::string payload_enc = token.substr(dot1 + 1, dot2 - dot1 - 1);
    std::string payload_json = base64url_decode(payload_enc);

    try {
        auto j = nlohmann::json::parse(payload_json);
        return j.value("exp", int64_t(0));
    } catch (...) {
        return 0;
    }
}

} // namespace kungfu::service::jwt
