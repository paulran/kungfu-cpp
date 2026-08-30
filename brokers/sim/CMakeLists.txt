# Simulated broker extension, built as an independently loadable shared
# library (kf_sim.dll on Windows / libkf_sim.so on Linux). kf_md and kf_td
# load it at runtime through kungfu::wingchun::plugin::Library and resolve
# the kf_sim_create_* factory symbols defined via the macros in
# kungfu/wingchun/extension.h.
set(SIM_EXTENSION_SOURCES
    src/sim_api.cpp
    src/market_data_sim.cpp
    src/order_book.cpp
    src/trader_sim.cpp
)

add_library(kf_sim SHARED ${SIM_EXTENSION_SOURCES})

target_include_directories(kf_sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

set_target_properties(kf_sim PROPERTIES LINKER_LANGUAGE CXX)

# kungfu transitively provides the include dirs of spdlog/fmt/nlohmann_json
# and the broker base classes the extension implements.
target_link_libraries(kf_sim PRIVATE ${LIBKUNGFU_NAME})
