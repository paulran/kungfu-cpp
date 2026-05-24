#include <kungfu/common/mmap.h>
#include <stdexcept>
#include <filesystem>

namespace kungfu::common {

#ifdef KUNGFU_WIN32
#include <windows.h>

static std::wstring utf8_to_wide(const std::string& str) {
    if (str.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), result.data(), size);
    return result;
}

MappedFile mmap_open(const std::string& path, size_t size, bool writable) {
    MappedFile mf{};
    mf.size = size;

    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    auto wpath = utf8_to_wide(path);
    DWORD access = writable ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ;
    DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
    DWORD creation = writable ? OPEN_ALWAYS : OPEN_EXISTING;

    mf.file_handle = CreateFileW(wpath.c_str(), access, share, nullptr,
                                  creation, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (mf.file_handle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Failed to open file: " + path);
    }

    if (writable) {
        LARGE_INTEGER li;
        li.QuadPart = static_cast<LONGLONG>(size);
        SetFilePointerEx(mf.file_handle, li, nullptr, FILE_BEGIN);
        SetEndOfFile(mf.file_handle);
    }

    DWORD protect = writable ? PAGE_READWRITE : PAGE_READONLY;
    LARGE_INTEGER map_size;
    map_size.QuadPart = static_cast<LONGLONG>(size);
    mf.map_handle = CreateFileMappingW(mf.file_handle, nullptr, protect,
                                        map_size.HighPart, map_size.LowPart, nullptr);
    if (!mf.map_handle) {
        CloseHandle(mf.file_handle);
        throw std::runtime_error("Failed to create file mapping: " + path);
    }

    DWORD map_access = writable ? FILE_MAP_ALL_ACCESS : FILE_MAP_READ;
    mf.address = MapViewOfFile(mf.map_handle, map_access, 0, 0, size);
    if (!mf.address) {
        CloseHandle(mf.map_handle);
        CloseHandle(mf.file_handle);
        throw std::runtime_error("Failed to map view of file: " + path);
    }

    return mf;
}

void mmap_close(MappedFile& mf) {
    if (mf.address) {
        FlushViewOfFile(mf.address, 0);
        UnmapViewOfFile(mf.address);
    }
    if (mf.map_handle) CloseHandle(mf.map_handle);
    if (mf.file_handle) CloseHandle(mf.file_handle);
    mf = {};
}

#else // POSIX

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

MappedFile mmap_open(const std::string& path, size_t size, bool writable) {
    MappedFile mf{};
    mf.size = size;

    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    int flags = writable ? (O_RDWR | O_CREAT) : O_RDONLY;
    mf.fd = ::open(path.c_str(), flags, 0644);
    if (mf.fd < 0) {
        throw std::runtime_error("Failed to open file: " + path);
    }

    if (writable) {
        if (ftruncate(mf.fd, static_cast<off_t>(size)) != 0) {
            ::close(mf.fd);
            throw std::runtime_error("Failed to ftruncate: " + path);
        }
    }

    int prot = writable ? (PROT_READ | PROT_WRITE) : PROT_READ;
    mf.address = ::mmap(nullptr, size, prot, MAP_SHARED, mf.fd, 0);
    if (mf.address == MAP_FAILED) {
        ::close(mf.fd);
        throw std::runtime_error("Failed to mmap: " + path);
    }

    return mf;
}

void mmap_close(MappedFile& mf) {
    if (mf.address && mf.address != MAP_FAILED) {
        ::munmap(mf.address, mf.size);
    }
    if (mf.fd >= 0) ::close(mf.fd);
    mf = {};
}

#endif

} // namespace kungfu::common
