add_executable(kf_strategy_sim sim_strategy_demo.cpp)
target_link_libraries(kf_strategy_sim PRIVATE kf_wingchun spdlog::spdlog)

add_executable(kf_strategy multi_process_strategy.cpp)
target_link_libraries(kf_strategy PRIVATE kf_wingchun spdlog::spdlog)
