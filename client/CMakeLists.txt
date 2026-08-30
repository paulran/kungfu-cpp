# Qt6 GUI client for kf_api — standalone build (header-only kungfu dependency).
#
# This CMakeLists.txt is designed to be built independently from the root
# kungfu-cpp project:
#
#   cmake -S client -B client/build -G "Visual Studio 17 2022" ^
#         -DCMAKE_PREFIX_PATH=D:/Qt/6.8.0/msvc2022_64
#   cmake --build client/build --config Release
#
# The client does NOT link against the kungfu shared library. It only needs
# the kungfu headers (for longfist POD struct definitions and nlohmann enum
# serialization). All 3rdparty deps are used in header-only mode.
#
# NOTE on moc: AUTOMOC is disabled on purpose. moc.exe in this environment is
# blocked by a Windows policy rule (hash-based AppLocker), so CMake could not
# run it at build time. The moc output for the Q_OBJECT classes is checked in
# under src/moc_*.cpp, hand-written to Qt6 revision-12 format. New UI widgets
# (OrderBook / OrderEntry / LeftBottomTabs) are plain QWidget subclasses
# WITHOUT Q_OBJECT, so they need no moc — they receive ApiClient signals via
# the function-pointer overload of QObject::connect.

cmake_minimum_required(VERSION 3.20)
project(kungfu-cpp-client VERSION 0.3.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# === Platform-specific flags (shared with the root project) ===
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/Platform.cmake)

# === Header-only 3rdparty include paths ===
# These are needed by kungfu headers (common.h, types.h, enums.h) which the
# client includes for POD struct definitions and nlohmann enum serialization.

set(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty)

# nlohmann_json (header-only, provides NLOHMANN_JSON_SERIALIZE_ENUM macros)
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${THIRD_PARTY_DIR}/nlohmann/single_include)

# fmt (header-only, needed by kungfu/common.h and spdlog/fmt/ostr.h includes)
add_library(fmt INTERFACE)
target_include_directories(fmt INTERFACE ${THIRD_PARTY_DIR}/fmt/include)

# spdlog (header-only, needed by kungfu/common.h and enums.h includes)
add_library(spdlog INTERFACE)
target_include_directories(spdlog INTERFACE ${THIRD_PARTY_DIR}/spdlog/include)
target_compile_definitions(spdlog INTERFACE SPDLOG_HEADER_ONLY SPDLOG_NO_NAME SPDLOG_NO_ATOMIC_LEVELS)

# Boost.Hana (header-only, needed by kungfu/common.h for BOOST_HANA_DEFINE_STRUCT)
add_library(boost_hana INTERFACE)
target_include_directories(boost_hana INTERFACE ${THIRD_PARTY_DIR}/hana/include)

# === Qt6 client executable ===

set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC OFF)
set(CMAKE_AUTOUIC OFF)

# Point CMake at the installed Qt6 kit if not already configured.
if(NOT DEFINED CMAKE_PREFIX_PATH OR CMAKE_PREFIX_PATH STREQUAL "")
    set(QT_KIT_PATH "D:/Qt/6.8.0/msvc2022_64" CACHE PATH "Qt6 kit root")
    list(APPEND CMAKE_PREFIX_PATH ${QT_KIT_PATH})
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

set(CLIENT_SOURCES
    src/main.cpp
    src/ApiClient.cpp
    src/ApiClient.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/StrategyTab.cpp
    src/StrategyTab.h
    # 4-quadrant trading terminal widgets (plain QWidget, no Q_OBJECT, no moc).
    src/OrderBook.cpp
    src/OrderBook.h
    src/OrderEntry.cpp
    src/OrderEntry.h
    src/LeftBottomTabs.cpp
    src/LeftBottomTabs.h
    # Hand-written moc output (no AUTOMOC). Only Q_OBJECT classes need these.
    src/moc_ApiClient.cpp
    src/moc_MainWindow.cpp
    src/moc_StrategyTab.cpp
    # Windows resource file (embeds app.ico as the executable icon, ID 1).
    app.rc
)

add_executable(kf_client WIN32 ${CLIENT_SOURCES})

target_include_directories(kf_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${THIRD_PARTY_DIR}/nlohmann/single_include
    ${THIRD_PARTY_DIR}/fmt/include
    ${THIRD_PARTY_DIR}/spdlog/include
    ${THIRD_PARTY_DIR}/hana/include
)

target_link_libraries(kf_client PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

if(WIN32)
    target_link_libraries(kf_client PRIVATE ws2_32)
endif()

# 拷贝 app.ico 到 exe 所在目录，供运行时 QApplication::setWindowIcon 加载
# （exe 自身图标由 app.rc 嵌入，此拷贝仅为窗口/任务栏运行时图标）。
add_custom_command(TARGET kf_client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico
            $<TARGET_FILE_DIR:kf_client>/app.ico)
