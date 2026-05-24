#pragma once

#include <string>
#include <cstddef>

namespace kungfu::common {

struct MappedFile {
    void* address = nullptr;
    size_t size = 0;
#ifdef KUNGFU_WIN32
    void* file_handle = nullptr;
    void* map_handle = nullptr;
#else
    int fd = -1;
#endif
};

MappedFile mmap_open(const std::string& path, size_t size, bool writable);
void mmap_close(MappedFile& mf);

} // namespace kungfu::common
