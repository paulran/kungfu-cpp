#include <kungfu/wingchun/broker/trader.h>
#include <kungfu/wingchun/gateway/sim/sim_td.h>
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
        auto location = io::location::make(io::category::TD, "sim", "sim", io::mode::LIVE);

        spdlog::info("kf_td_sim: starting, uid={}", location->uid);

        broker::TraderVendor vendor(location, locator, cfg.system.low_latency);

        auto md_location = io::location::make(io::category::MD, "sim", "sim", io::mode::LIVE);
        vendor.register_location(md_location);
        vendor.request_read_from(md_location, 0);

        auto sim_td = std::make_unique<gateway::sim::SimTrader>();

        sim_td->set_write_callback([&vendor](const longfist::types::Order& order, const longfist::types::Trade* trade) {
            auto writer = vendor.get_writer(vendor.home(), 0);
            if (trade) {
                writer->write(0, *trade);
            }
            writer->write(0, order);
        });

        vendor.set_service(std::move(sim_td));
        vendor.run();
    } catch (const std::exception& e) {
        spdlog::error("kf_td_sim: fatal error: {}", e.what());
        return 1;
    }

    return 0;
}
