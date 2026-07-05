add_executable(kf_master master.cpp)
target_link_libraries(kf_master PRIVATE ${LIBKUNGFU_NAME} spdlog::spdlog)