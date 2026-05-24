add_library(kf_wingchun STATIC
    broker/broker.cpp
    broker/market_data.cpp
    broker/trader.cpp
    broker/client.cpp
    gateway/sim/matching_engine.cpp
    gateway/sim/sim_md.cpp
    gateway/sim/sim_td.cpp
    strategy/runner.cpp
    strategy/runtime_context.cpp
    strategy/sim_context.cpp
    ${PROJECT_SOURCE_DIR}/src/services/ledger/book.cpp
)
target_include_directories(kf_wingchun PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(kf_wingchun PUBLIC kf_yijinjing kf_common kf_longfist spdlog::spdlog)

# SIM gateway executables
add_executable(kf_md_sim gateway/sim/main_md.cpp)
target_link_libraries(kf_md_sim PRIVATE kf_wingchun spdlog::spdlog)

add_executable(kf_td_sim gateway/sim/main_td.cpp)
target_link_libraries(kf_td_sim PRIVATE kf_wingchun spdlog::spdlog)
