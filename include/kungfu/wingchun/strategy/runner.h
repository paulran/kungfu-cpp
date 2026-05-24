#pragma once

#include <kungfu/wingchun/strategy/strategy.h>
#include <kungfu/wingchun/strategy/context.h>
#include <kungfu/yijinjing/practice/apprentice.h>
#include <memory>

namespace kungfu::wingchun::strategy {

class Runner : public yijinjing::practice::apprentice {
public:
    Runner(const yijinjing::io::location_ptr& home,
           yijinjing::io::Locator& locator, bool low_latency);

    void set_strategy(std::shared_ptr<Strategy> strategy);
    void set_context(std::unique_ptr<Context> context);

    Strategy* strategy() { return strategy_.get(); }
    Context* context() { return context_.get(); }

    void react() override;
    void on_start() override;
    void on_exit() override;

private:
    std::shared_ptr<Strategy> strategy_;
    std::unique_ptr<Context> context_;
};

} // namespace kungfu::wingchun::strategy
