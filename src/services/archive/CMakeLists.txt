add_executable(kf_archive main.cpp archive.cpp)
target_include_directories(kf_archive PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(kf_archive PRIVATE kf_yijinjing kf_common kf_longfist spdlog::spdlog)
