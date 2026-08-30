#pragma once

// Minimal loader for dynamically loaded wingchun extensions (e.g. the sim
// broker built as kf_sim.dll / libkf_sim.so). Extension objects (broker
// services) are allocated inside the extension module and wrapped into
// std::shared_ptr by the host, so the library is intentionally never unloaded:
// its vtables/code must stay mapped for the whole process lifetime. The OS
// reclaims the mapping at process exit.

#include <memory>
#include <spdlog/spdlog.h>
#include <string>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace kungfu::wingchun::plugin {

class Library {
public:
  /// Loads an extension by its bare name, e.g. "sim" resolves to kf_sim.dll
  /// (Windows), libsim.so (Linux) or libsim.dylib (macOS). A name containing
  /// a path is passed through as-is. Returns nullptr and logs on failure.
  static std::shared_ptr<Library> load(const std::string &name) {
#ifdef _WIN32
    std::string file = name + ".dll";
#elif defined(__APPLE__)
    std::string file = "lib" + name + ".dylib";
#else
    std::string file = "lib" + name + ".so";
#endif
    auto library = std::shared_ptr<Library>(new Library(file));
    if (!library->handle_) {
#ifdef _WIN32
      SPDLOG_ERROR("failed to load extension library {}: error {}", file, GetLastError());
#else
      SPDLOG_ERROR("failed to load extension library {}: {}", file, dlerror());
#endif
      return nullptr;
    }
    return library;
  }

  [[nodiscard]] void *symbol(const std::string &name) const {
#ifdef _WIN32
    return reinterpret_cast<void *>(GetProcAddress(handle_, name.c_str()));
#else
    return dlsym(handle_, name.c_str());
#endif
  }

  template <typename Fn> [[nodiscard]] Fn symbol_as(const std::string &name) const {
    return reinterpret_cast<Fn>(symbol(name));
  }

  [[nodiscard]] const std::string &file() const { return file_; }

private:
  explicit Library(std::string file) : file_(std::move(file)) {
#ifdef _WIN32
    handle_ = LoadLibraryA(file_.c_str());
#else
    // RTLD_LOCAL keeps extension symbols from interposing each other, which
    // matters because every extension exports the same factory symbol names
    // (kf_create_trader / kf_create_market_data, see wingchun/extension.h).
    handle_ = dlopen(file_.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
  }

  std::string file_;
#ifdef _WIN32
  HMODULE handle_ = nullptr;
#else
  void *handle_ = nullptr;
#endif
};

} // namespace kungfu::wingchun::plugin
