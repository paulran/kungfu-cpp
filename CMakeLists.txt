cmake_minimum_required(VERSION 3.20)
project(kungfu-cpp VERSION 0.2.0 LANGUAGES CXX C)

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

# fmt
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/fmt EXCLUDE_FROM_ALL)
set_target_properties(fmt PROPERTIES POSITION_INDEPENDENT_CODE ON)

# spdlog
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/spdlog EXCLUDE_FROM_ALL)

# rxcpp (header-only)
add_library(rxcpp INTERFACE)
target_include_directories(rxcpp INTERFACE ${CMAKE_SOURCE_DIR}/3rdparty/rxcpp/Rx/v2/src)

# sqlite3
set(SQLITE_ENABLE_COLUMN_METADATA ON CACHE BOOL "" FORCE)
set(SQLITE_OMIT_DECLTYPE OFF CACHE BOOL "" FORCE)
set(BUILD_SHELL OFF CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/sqlite3 EXCLUDE_FROM_ALL)
target_include_directories(SQLite3 PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/3rdparty/sqlite3>)

# sqlite_orm (header-only)
add_library(sqlite_orm INTERFACE)
target_include_directories(sqlite_orm INTERFACE ${CMAKE_SOURCE_DIR}/3rdparty/sqlite_orm/include)
target_link_libraries(sqlite_orm INTERFACE SQLite3)

# tomlplusplus (header-only)
add_library(tomlplusplus INTERFACE)
target_include_directories(tomlplusplus INTERFACE ${CMAKE_SOURCE_DIR}/3rdparty/tomlplusplus/include)

# tabulate (header-only)
add_library(tabulate INTERFACE)
target_include_directories(tabulate INTERFACE ${CMAKE_SOURCE_DIR}/3rdparty/tabulate/include)

# === Project libraries ===
add_subdirectory(src)

# === Examples ===
add_subdirectory(examples)

# === Apps ===
add_subdirectory(apps)

# === Tests ===
# Tests directory does not exist yet
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
