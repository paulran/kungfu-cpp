#include <kungfu/wingchun/strategy/runner.h>
#include <spdlog/spdlog.h>

namespace kungfu::wingchun::strategy {

Runner::Runner(const yijinjing::io::location_ptr& home,
               yijinjing::io::Locator& locator, bool low_latency)
    : apprentice(home, locator, low_latency) {}

void Runner::set_strategy(std::shared_ptr<Strategy> strategy) {
    strategy_ = std::move(strategy);
}

void Runner::set_context(std::unique_ptr<Context> context) {
    context_ = std::move(context);
}

void Runner::react() {
    // In full implementation, this would dispatch journal frames to context/strategy
}

void Runner::on_start() {
    if (strategy_ && context_) {
        strategy_->pre_start(*context_);
        strategy_->post_start(*context_);
    }
}

void Runner::on_exit() {
    if (strategy_ && context_) {
        strategy_->pre_stop(*context_);
        strategy_->post_stop(*context_);
    }
}

} // namespace kungfu::wingchun::strategy
