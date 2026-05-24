add_executable(kf_ledger main.cpp ledger.cpp book.cpp)
target_include_directories(kf_ledger PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(kf_ledger PRIVATE kf_yijinjing kf_common kf_longfist spdlog::spdlog)
