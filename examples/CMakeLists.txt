add_executable(kf_strategy_sim sim_strategy_demo.cpp)
target_link_libraries(kf_strategy_sim PRIVATE kf_wingchun spdlog::spdlog)

add_executable(kf_strategy multi_process_strategy.cpp)
target_link_libraries(kf_strategy PRIVATE kf_wingchun spdlog::spdlog)

add_executable(kf_api_gateway_demo api_gateway_demo.cpp nng_http_client.cpp)
target_include_directories(kf_api_gateway_demo PRIVATE
    ${PROJECT_SOURCE_DIR}/3rdparty/nng/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(kf_api_gateway_demo PRIVATE nng nlohmann_json::nlohmann_json spdlog::spdlog)
