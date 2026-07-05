project(${LIBKUNGFU_NAME})

enable_windows_export_all_symbols()

include_directories(
    ${PROJECT_SOURCE_DIR}/../include
    ${PROJECT_SOURCE_DIR}/../3rdparty/hana/include
    ${PROJECT_SOURCE_DIR}/../3rdparty/sqlite_orm/include
    ${PROJECT_SOURCE_DIR}/../3rdparty/rxcpp/Rx/v2/src
    ${PROJECT_SOURCE_DIR}/../3rdparty/spdlog/include
    ${PROJECT_SOURCE_DIR}/../3rdparty/fmt/include
    ${PROJECT_SOURCE_DIR}/../3rdparty/nlohmann/single_include
    ${PROJECT_SOURCE_DIR}/../3rdparty/nng/include
    ${PROJECT_SOURCE_DIR}/../3rdparty/tabulate/include
)

file(GLOB_RECURSE KUNGFU_OPTIM_SOURCE_FILES ${PROJECT_SOURCE_DIR}/*.cpp)
file(GLOB_RECURSE KUNGFU_CACHE_SOURCE_FILES ${PROJECT_SOURCE_DIR}/yijinjing/cache/*.cpp)
list(REMOVE_ITEM KUNGFU_OPTIM_SOURCE_FILES ${KUNGFU_CACHE_SOURCE_FILES})

add_library_object(kungfu_cache "${KUNGFU_CACHE_SOURCE_FILES}" "${COMPILER_OPTIMIZE_OFF_OPTIONS}" "${KUNGFU_BUILD_DIR}")
add_library_object(kungfu_optim "${KUNGFU_OPTIM_SOURCE_FILES}" "${COMPILER_OPTIMIZE_ON_OPTIONS}" "${KUNGFU_BUILD_DIR}")

add_library(${LIBKUNGFU_NAME} SHARED $<TARGET_OBJECTS:kungfu_optim> $<TARGET_OBJECTS:kungfu_cache>)
set_target_properties(${LIBKUNGFU_NAME} PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(${LIBKUNGFU_NAME} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${KUNGFU_BUILD_DIR})
target_compile_options(${LIBKUNGFU_NAME} PRIVATE "$<$<CONFIG:Release>:${COMPILER_OPTIMIZE_ON_OPTIONS}>")

target_include_directories(${LIBKUNGFU_NAME} PUBLIC
    ${PROJECT_SOURCE_DIR}/../include
    ${PROJECT_SOURCE_DIR}/../3rdparty/hana/include
    ${PROJECT_SOURCE_DIR}/../3rdparty/sqlite_orm/include
    ${PROJECT_SOURCE_DIR}/../3rdparty/rxcpp/Rx/v2/src
    ${PROJECT_SOURCE_DIR}/../3rdparty/spdlog/include
    ${PROJECT_SOURCE_DIR}/../3rdparty/fmt/include
    ${PROJECT_SOURCE_DIR}/../3rdparty/nlohmann/single_include
    ${PROJECT_SOURCE_DIR}/../3rdparty/nng/include
    ${PROJECT_SOURCE_DIR}/../3rdparty/tabulate/include
)

target_link_libraries(${LIBKUNGFU_NAME}
    boost_hana
    rxcpp
    sqlite_orm
    nng
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    tomlplusplus
    tabulate
    fmt::fmt
)