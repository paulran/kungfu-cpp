add_executable(kf_cached main.cpp cached.cpp store.cpp)
target_include_directories(kf_cached PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(kf_cached PRIVATE kf_yijinjing kf_common kf_longfist sqlite_orm spdlog::spdlog)
