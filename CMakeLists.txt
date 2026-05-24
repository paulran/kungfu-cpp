cmake_minimum_required(VERSION 3.20)
project(kungfu-cpp VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_TESTS "Build unit tests" ON)

include(cmake/Platform.cmake)

# === Third-party dependencies ===

# Boost.Hana (header-only)
add_library(boost_hana INTERFACE)
target_include_directories(boost_hana INTERFACE ${CMAKE_SOURCE_DIR}/3rdparty/hana/include)

# nng
set(NNG_TESTS OFF CACHE BOOL "" FORCE)
set(NNG_TOOLS OFF CACHE BOOL "" FORCE)
set(NNG_ENABLE_NNGCAT OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/nng EXCLUDE_FROM_ALL)

# nlohmann_json (header-only)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/nlohmann EXCLUDE_FROM_ALL)

# spdlog
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/spdlog EXCLUDE_FROM_ALL)

# rxcpp (header-only)
add_library(rxcpp INTERFACE)
target_include_directories(rxcpp INTERFACE ${CMAKE_SOURCE_DIR}/3rdparty/rxcpp/Rx/v2/src)

# === Project libraries ===
add_subdirectory(src)

# === Tests ===
if(BUILD_TESTS)
    enable_testing()
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    add_subdirectory(3rdparty/googletest EXCLUDE_FROM_ALL)
    add_subdirectory(tests)
endif()
