#pragma once

#include <deque>
#include <functional>
#include <mutex>
#include <set>
#include <vector>

#include <kungfu/wingchun/broker/marketdata.h>
#include <ThostFtdcMdApi.h>

#include "ctp_common.h"

namespace kungfu::wingchun::ctp {

class CtpMdSpi;

class MarketDataCtp : public broker::MarketData {
public:
  explicit MarketDataCtp(broker::MarketDataVendor &vendor);

  ~MarketDataCtp() override;

  void on_start() override;

  void on_exit() override;

  bool subscribe(const std::vector<longfist::types::InstrumentKey> &instrument_keys) override;

  bool unsubscribe(const std::vector<longfist::types::InstrumentKey> &instrument_keys) override;

  // ==== CTP SPI callbacks (CTP worker thread, enqueue only) ====

  void on_login_success(const CThostFtdcRspUserLoginField &login);

  void on_login_failed(const CThostFtdcRspInfoField &error);

  void on_front_disconnected(int reason);

  void on_depth_market_data(const CThostFtdcDepthMarketDataField &depth);

private:
  using Task = std::function<void()>;

  void enqueue(Task task);

  void drain_tasks();

  bool do_subscribe(const std::vector<std::string> &instrument_ids);

  CThostFtdcMdApi *api_ = nullptr;
  CtpMdSpi *spi_ = nullptr;
  CtpConfig config_;

  // All members below are only touched on the kungfu event loop thread.
  bool logged_in_ = false;
  bool started_ = false;
  std::set<std::string> subscribed_;
  std::vector<std::string> pending_subscribes_;
  std::deque<Task> tasks_;
  std::mutex tasks_mutex_;
};

} // namespace kungfu::wingchun::ctp
