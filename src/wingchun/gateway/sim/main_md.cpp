#include <kungfu/wingchun/broker/market_data.h>
#include <kungfu/wingchun/gateway/sim/sim_md.h>
#include <kungfu/common/config.h>
#include <spdlog/spdlog.h>

using namespace kungfu;
using namespace kungfu::yijinjing;
using namespace kungfu::wingchun;

int main(int argc, char** argv) {
    std::string config_path = argc > 1 ? argv[1] : "kungfu.toml";

    try {
        auto cfg = common::KungfuConfig::load(config_path);
        spdlog::set_level(spdlog::level::from_str(cfg.system.log_level));

        io::Locator locator(cfg.system.home);
        auto location = io::location::make(io::category::MD, "sim", "sim", io::mode::LIVE);

        spdlog::info("kf_md_sim: starting, uid={}", location->uid);

        broker::MarketDataVendor vendor(location, locator, cfg.system.low_latency);

        auto sim_md = std::make_unique<gateway::sim::SimMarketData>();

        sim_md->set_write_callback([&vendor](const longfist::types::Quote& q) {
            auto writer = vendor.get_writer(vendor.home(), 0);
            writer->write(0, q);
        });

        sim_md->subscribe("600000", "SSE", longfist::enums::InstrumentType::Stock);
        spdlog::info("kf_md_sim: pre-subscribed 600000@SSE");

        vendor.set_service(std::move(sim_md));
        vendor.run();
    } catch (const std::exception& e) {
        spdlog::error("kf_md_sim: fatal error: {}", e.what());
        return 1;
    }

    return 0;
}
