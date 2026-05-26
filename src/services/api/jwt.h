#pragma once

#include <string>
#include <cstdint>

namespace kungfu::service::jwt {

// HMAC-SHA256 based JWT token generation and validation
// Minimal implementation - no external crypto dependency

std::string create_token(const std::string& secret,
                         const std::string& username,
                         int64_t expire_time_s);

bool verify_token(const std::string& secret,
                  const std::string& token);

std::string get_username(const std::string& token);

int64_t get_expiry(const std::string& token);

} // namespace kungfu::service::jwt
