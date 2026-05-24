#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <functional>

namespace kungfu {

using int64_nano = int64_t;

constexpr uint32_t PUBLIC_DEST = 0;
constexpr uint32_t SYNC_DEST = 1;

constexpr uint32_t JOURNAL_VERSION = 1;
constexpr uint32_t DEFAULT_PAGE_SIZE = 1 * 1024 * 1024;
constexpr uint32_t MD_PAGE_SIZE = 128 * 1024 * 1024;
constexpr uint32_t TD_PAGE_SIZE = 16 * 1024 * 1024;

} // namespace kungfu
