#pragma once

#include <cstdint>
#include <string>

namespace kungfu::common {

uint32_t hash_32(const void* key, int len, uint32_t seed);
uint32_t hash_str_32(const std::string& key, uint32_t seed = 42);

} // namespace kungfu::common
