add_executable(kf_master master.cpp)
target_link_libraries(kf_master PRIVATE ${LIBKUNGFU_NAME} spdlog::spdlog)

add_executable(kf_md md.cpp)
target_link_libraries(kf_md PRIVATE ${LIBKUNGFU_NAME} spdlog::spdlog)

add_executable(kf_strategy strategy.cpp)
target_link_libraries(kf_strategy PRIVATE ${LIBKUNGFU_NAME} spdlog::spdlog)

add_executable(kf_cached cached.cpp)
target_link_libraries(kf_cached PRIVATE ${LIBKUNGFU_NAME} spdlog::spdlog)

add_executable(kf_ledger ledger.cpp)
target_link_libraries(kf_ledger PRIVATE ${LIBKUNGFU_NAME} spdlog::spdlog)

add_executable(kf_td td.cpp)
target_link_libraries(kf_td PRIVATE ${LIBKUNGFU_NAME} spdlog::spdlog)

add_executable(kf_api api.cpp)
target_link_libraries(kf_api PRIVATE ${LIBKUNGFU_NAME} spdlog::spdlog)
if(WIN32)
    target_link_libraries(kf_api PRIVATE ws2_32)
endif()