add_library(kf_common STATIC
    hash.cpp
    mmap.cpp
    config.cpp
)
target_include_directories(kf_common PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(kf_common PUBLIC spdlog::spdlog tomlplusplus)
if(WIN32)
    target_link_libraries(kf_common PUBLIC kernel32)
endif()
