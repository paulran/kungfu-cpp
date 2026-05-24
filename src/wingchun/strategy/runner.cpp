#include <kungfu/wingchun/strategy/runner.h>
#include <kungfu/wingchun/strategy/runtime_context.h>
#include <kungfu/longfist/types.h>
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
    events_.subscribe(
        lifetime_,
        [this](yijinjing::practice::event_ptr event) {
            auto* ctx = dynamic_cast<RuntimeContext*>(context_.get());
            if (!ctx || !strategy_) return;

            switch (event->msg_type()) {
                case longfist::types::Quote::tag: {
                    const auto& quote = event->data<longfist::types::Quote>();
                    ctx->on_quote(quote);
                    strategy_->on_quote(*context_, quote);
                    break;
                }
                case longfist::types::Order::tag: {
                    const auto& order = event->data<longfist::types::Order>();
                    ctx->on_order(order);
                    strategy_->on_order(*context_, order);
                    break;
                }
                case longfist::types::Trade::tag: {
                    const auto& trade = event->data<longfist::types::Trade>();
                    ctx->on_trade(trade);
                    strategy_->on_trade(*context_, trade);
                    break;
                }
                default:
                    break;
            }
        },
        [](std::exception_ptr ep) {
            try { if (ep) std::rethrow_exception(ep); }
            catch (const std::exception& e) {
                spdlog::error("Runner: event error: {}", e.what());
            }
        }
    );
}

void Runner::on_start() {
    apprentice::on_start();
    if (strategy_ && context_) {
        strategy_->pre_start(*context_);
        strategy_->post_start(*context_);
    }
    spdlog::info("Runner: strategy started, uid={}", home_uid());
}

void Runner::on_active() {
    apprentice::on_active();
}

void Runner::on_exit() {
    if (strategy_ && context_) {
        strategy_->pre_stop(*context_);
        strategy_->post_stop(*context_);
    }
    spdlog::info("Runner: strategy stopped");
}

void Runner::on_channel(uint32_t source_uid, uint32_t dest_uid) {
    spdlog::info("Runner: channel notification source={} dest={}", source_uid, dest_uid);
}

} // namespace kungfu::wingchun::strategy
