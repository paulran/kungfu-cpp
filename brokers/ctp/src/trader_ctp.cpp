#include "trader_ctp.h"

#include <filesystem>
#include <ctime>

#include <kungfu/wingchun/common.h>
#include <kungfu/yijinjing/time.h>
#include <spdlog/spdlog.h>

#include "ctp_trader_spi.h"

namespace kungfu::wingchun::ctp {

using namespace kungfu::longfist::types;
using namespace kungfu::longfist::enums;

namespace {
std::string resolve_exchange_id(const std::string &exchange_id, const std::string &instrument_id) {
  if (!exchange_id.empty()) {
    return exchange_id;
  }
  std::string inferred = get_exchange_id_from_future_instrument_id(instrument_id);
  return inferred.empty() ? "FUTURE" : inferred;
}

TimeCondition time_condition_from_ctp(char time_condition) {
  switch (time_condition) {
  case THOST_FTDC_TC_IOC:
    return TimeCondition::IOC;
  case THOST_FTDC_TC_GTC:
    return TimeCondition::GTC;
  case THOST_FTDC_TC_GFD:
  default:
    return TimeCondition::GFD;
  }
}

VolumeCondition volume_condition_from_ctp(char volume_condition) {
  switch (volume_condition) {
  case THOST_FTDC_VC_MV:
    return VolumeCondition::Min;
  case THOST_FTDC_VC_CV:
    return VolumeCondition::All;
  case THOST_FTDC_VC_AV:
  default:
    return VolumeCondition::Any;
  }
}
} // namespace

TraderCtp::TraderCtp(broker::TraderVendor &vendor) : Trader(vendor) {
}

TraderCtp::~TraderCtp() {
  if (api_ != nullptr) {
    api_->Release();
    api_ = nullptr;
  }
}

AccountType TraderCtp::get_account_type() const { return AccountType::Future; }

void TraderCtp::on_start() {
  config_ = parse_config(get_config());
  if (config_.front_uri.empty()) {
    SPDLOG_ERROR("CTP TD front_uri is empty, cannot connect");
    return;
  }

  std::filesystem::path flow_path = std::filesystem::path(get_runtime_folder()) / "ctp_td_flow";
  std::filesystem::create_directories(flow_path);

  api_ = CThostFtdcTraderApi::CreateFtdcTraderApi(flow_path.string().c_str());
  spi_ = new CtpTraderSpi(api_, this, config_);
  api_->RegisterSpi(spi_);
  api_->RegisterFront(config_.front_uri.data());
  api_->SubscribePrivateTopic(THOST_TERT_RESUME); // 从上次收到的续传
  api_->SubscribePublicTopic(THOST_TERT_QUICK); // 只传送登录后公共流的内容
  api_->Init();

  add_time_interval(yijinjing::time_unit::NANOSECONDS_PER_MILLISECOND, [&](const event_ptr &) { drain_tasks(); });
  // CTP allows at most one query per second; drain the queue slower to be safe.
  add_time_interval(yijinjing::time_unit::NANOSECONDS_PER_SECOND * 1.1, [&](const event_ptr &) { pump_query_queue(); });

  update_broker_state(BrokerState::Idle);
  SPDLOG_INFO("CTP TD starting, front {}", config_.front_uri);
}

void TraderCtp::on_exit() {
  if (api_ != nullptr) {
    api_->Release();
    api_ = nullptr;
  }
}

void TraderCtp::on_recover() {
  // recover() already rebuilt orders_ from today's PUBLIC frames; use them to
  // restore OrderRef -> order_id mappings so cancels and async callbacks keep
  // working for orders placed before a restart.
  for (auto &pair : orders_) {
    const Order &order = pair.second.data;
    std::string order_ref = order.external_order_id.to_string();
    if (order_ref.empty()) {
      continue;
    }
    if (ref_to_order_id_.find(order_ref) != ref_to_order_id_.end()) {
      continue;
    }
    ref_to_order_id_[order_ref] = order.order_id;
    order_id_to_ref_info_[order.order_id] = {order_ref, order.instrument_id.to_string(),
                                             order.exchange_id.to_string(), pair.second.dest};
  }
}

bool TraderCtp::is_ready_for_requests() const { return api_ != nullptr && logged_in_ && ready_; }

// ==== queueing ====

void TraderCtp::enqueue(Task task) {
  {
    std::lock_guard<std::mutex> guard(tasks_mutex_);
    tasks_.emplace_back(std::move(task));
  }
}

void TraderCtp::drain_tasks() {
  std::deque<Task> pending;
  {
    std::lock_guard<std::mutex> guard(tasks_mutex_);
    pending.swap(tasks_);
  }
  while (!pending.empty()) {
    auto task = std::move(pending.front());
    pending.pop_front();
    task();
  }
}

void TraderCtp::push_query(Task query) { query_queue_.emplace_back(std::move(query)); }

void TraderCtp::finish_in_flight_query() {
  // Only reset; do not pump here to avoid recursion when the caller is itself
  // running inside pump_query_queue. The next 1.1s tick picks up the next query.
  query_in_flight_ = false;
}

void TraderCtp::pump_query_queue() {
  if (query_in_flight_) {
    // Previous query is still waiting for its bIsLast/end callback. Sending a
    // new one now would make CTP return -2 (request too frequent / overlap).
    // Force-reset only after QUERY_TIMEOUT_NS so a lost callback cannot stall
    // the queue forever (instrument paging tops out around 30s).
    if (yijinjing::time::now_in_nano() - query_started_at_nano_ < QUERY_TIMEOUT_NS) {
      return;
    }
    SPDLOG_WARN("CTP TD query in-flight timeout ({}s), force reset", QUERY_TIMEOUT_NS / 1000000000);
    query_in_flight_ = false;
  }
  if (query_queue_.empty()) {
    return;
  }
  if (!is_ready_for_requests()) {
    SPDLOG_WARN("CTP TD not ready, dropping {} queued queries", query_queue_.size());
    query_queue_.clear();
    return;
  }
  auto query = std::move(query_queue_.front());
  query_queue_.pop_front();
  // The lambda returns true if it submitted a CTP request and is now waiting
  // for the matching bIsLast/end callback to clear query_in_flight_. Returns
  // false for placeholder / send-failure (no callback will come), so pump
  // leaves query_in_flight_ false and the next tick picks up the next query.
  if (query()) {
    query_in_flight_ = true;
    query_started_at_nano_ = yijinjing::time::now_in_nano();
  }
}

// ==== SPI callbacks ====

void TraderCtp::on_front_disconnected(int reason) {
  enqueue([this, reason]() -> bool {
    logged_in_ = false;
    ready_ = false;
    update_broker_state(BrokerState::DisConnected);
    SPDLOG_WARN("CTP TD front disconnected ({}), waiting for auto reconnect", reason);
    return false;
  });
}

void TraderCtp::on_login_success(const CThostFtdcRspUserLoginField &login) {
  enqueue([this, login]() -> bool {
    front_id_ = login.FrontID;
    session_id_ = login.SessionID;
    trading_day_ = login.TradingDay;
    logged_in_ = true;
    update_broker_state(BrokerState::LoggedIn);
    SPDLOG_INFO("CTP TD logged in, trading day {}, front {}, session {}", trading_day_, front_id_, session_id_);

    // 登录后先查询结算单确认状态：当日已确认→直接 Ready；否则拉取结算单
    // 再调用 ReqSettlementInfoConfirm 发起确认。ReqSettlementInfoConfirm 的回调
    // 是 OnRspSettlementInfoConfirm（确认动作响应），ReqQrySettlementInfoConfirm
    // 的回调是 OnRspQrySettlementInfoConfirm（查询响应），二者不可混用，否则会
    // 出现"请求发出但永远收不到响应"的现象。
    CThostFtdcQrySettlementInfoConfirmField qry = {0};
    strncpy(qry.BrokerID, config_.broker_id.c_str(), sizeof(qry.BrokerID) - 1);
    strncpy(qry.InvestorID, config_.investor_id.c_str(), sizeof(qry.InvestorID) - 1);
    int ret = api_->ReqQrySettlementInfoConfirm(&qry, ++request_id_);
    if (ret != 0) {
      SPDLOG_ERROR("failed to send qry settlement info confirm, error {}", ret);
    } else {
      SPDLOG_INFO("CTP TD sent qry settlement info confirm, broker {} investor {} waiting for OnRspQrySettlementInfoConfirm",
                   qry.BrokerID, qry.InvestorID);
    }
    return false;
  });
}

void TraderCtp::on_login_failed(const CThostFtdcRspInfoField &error) {
  enqueue([this, error]() -> bool {
    logged_in_ = false;
    ready_ = false;
    update_broker_state(BrokerState::LoginFailed);
    SPDLOG_ERROR("CTP TD login failed, error {}: {}", error.ErrorID, error.ErrorMsg);
    return false;
  });
}

void TraderCtp::on_settlement_confirmed() {
  enqueue([this]() -> bool {
    ready_ = true;
    update_broker_state(BrokerState::Ready);

    push_query([this]() -> bool {
      SPDLOG_INFO("CTP TD start querying initial data, this is a placeholder.");
      // 不发 CTP 请求，没有回调会清 in_flight，返回 false 让 pump 放行下一个。
      return false;
    });

    push_query([this]() -> bool {
      CThostFtdcQryInstrumentField req = {};
      // strncpy(req.ExchangeID, "SHFE", sizeof(req.ExchangeID) - 1);
      // strncpy(req.InstrumentID, "au2610", sizeof(req.InstrumentID) - 1);
      int ret = api_->ReqQryInstrument(&req, ++request_id_);
      if (ret != 0) {
        SPDLOG_ERROR("failed to send qry instrument, error {}", ret);
        return false;
      }
      SPDLOG_INFO("CTP TD qry instrument, exchange {}, instrument {}", req.ExchangeID, req.InstrumentID);
      return true;
    });
    push_query([this]() -> bool {
      CThostFtdcQryTradingAccountField req = {};
      strncpy(req.BrokerID, config_.broker_id.c_str(), sizeof(req.BrokerID) - 1);
      strncpy(req.InvestorID, config_.investor_id.c_str(), sizeof(req.InvestorID) - 1);
      int ret = api_->ReqQryTradingAccount(&req, ++request_id_);
      if (ret != 0) {
        SPDLOG_ERROR("failed to send qry trading account, error {}", ret);
        return false;
      }
      return true;
    });
    push_query([this]() -> bool {
      CThostFtdcQryInvestorPositionField req = {};
      strncpy(req.BrokerID, config_.broker_id.c_str(), sizeof(req.BrokerID) - 1);
      strncpy(req.InvestorID, config_.investor_id.c_str(), sizeof(req.InvestorID) - 1);
      int ret = api_->ReqQryInvestorPosition(&req, ++request_id_);
      if (ret != 0) {
        SPDLOG_ERROR("failed to send qry investor position, error {}", ret);
        return false;
      }
      return true;
    });
    push_query([this]() -> bool {
      CThostFtdcQryOrderField req = {};
      strncpy(req.InsertTimeStart, "00:00:00", sizeof(req.InsertTimeStart) - 1);
      strncpy(req.InsertTimeEnd, "23:59:59", sizeof(req.InsertTimeEnd) - 1);
      int ret = api_->ReqQryOrder(&req, ++request_id_);
      if (ret != 0) {
        SPDLOG_ERROR("failed to send qry order, error {}", ret);
        return false;
      }
      return true;
    });
    push_query([this]() -> bool {
      CThostFtdcQryTradeField req = {};
      strncpy(req.BrokerID, config_.broker_id.c_str(), sizeof(req.BrokerID) - 1);
      strncpy(req.InvestorID, config_.investor_id.c_str(), sizeof(req.InvestorID) - 1);
      int ret = api_->ReqQryTrade(&req, ++request_id_);
      if (ret != 0) {
        SPDLOG_ERROR("failed to send qry trade, error {}", ret);
        return false;
      }
      return true;
    });
    SPDLOG_INFO("CTP TD ready, initial queries queued");
    return false;
  });
}

namespace {
// YYYYMMDD 格式的当前系统日期，用于比对 SettlementInfoConfirm.ConfirmDate
// 判断结算单是否已在当日被确认。
std::string current_date_yyyymmdd() {
  std::time_t now = std::time(nullptr);
  std::tm *tm = std::localtime(&now);
  char buf[16] = {0};
  std::strftime(buf, sizeof(buf), "%Y%m%d", tm);
  return buf;
}
} // namespace

void TraderCtp::on_qry_settlement_info_confirm(const CThostFtdcSettlementInfoConfirmField *confirm, bool is_last) {
  if (!is_last) {
    return;
  }
  enqueue([this, confirm]() -> bool {
    std::string today = current_date_yyyymmdd();
    bool confirmed_today = (confirm != nullptr) && (today == (const char *)confirm->ConfirmDate);
    if (confirmed_today) {
      SPDLOG_INFO("CTP TD settlement already confirmed today ({}), skip re-confirm", today);
      on_settlement_confirmed();
      return false;
    }
    if (confirm) {
      SPDLOG_INFO("CTP TD settlement last confirmed on {}, need re-confirm for today {}",
                  (const char *)confirm->ConfirmDate, today);
    } else {
      SPDLOG_INFO("CTP TD no prior settlement confirm record, need confirm for today {}", today);
    }
    // 拉取结算单正文（ReqQrySettlementInfo），响应到 OnRspQrySettlementInfo 后
    // 在 bIsLast 时发起 ReqSettlementInfoConfirm，避免对已确认日重复确认。
    CThostFtdcQrySettlementInfoField qry = {0};
    strncpy(qry.BrokerID, config_.broker_id.c_str(), sizeof(qry.BrokerID) - 1);
    strncpy(qry.InvestorID, config_.investor_id.c_str(), sizeof(qry.InvestorID) - 1);
    strncpy(qry.TradingDay, trading_day_.c_str(), sizeof(qry.TradingDay) - 1);
    int ret = api_->ReqQrySettlementInfo(&qry, ++request_id_);
    if (ret != 0) {
      SPDLOG_ERROR("failed to send qry settlement info, error {}", ret);
    }
    return false;
  });
}

void TraderCtp::on_qry_settlement_info(const CThostFtdcSettlementInfoField *info, bool is_last) {
  if (!is_last) {
    return;
  }
  enqueue([this]() -> bool {
    // 结算单正文已由前置回调读取，此处直接发起确认。
    CThostFtdcSettlementInfoConfirmField req = {0};
    strncpy(req.BrokerID, config_.broker_id.c_str(), sizeof(req.BrokerID) - 1);
    strncpy(req.InvestorID, config_.investor_id.c_str(), sizeof(req.InvestorID) - 1);
    int ret = api_->ReqSettlementInfoConfirm(&req, ++request_id_);
    if (ret != 0) {
      SPDLOG_ERROR("failed to send settlement info confirm, error {}", ret);
    } else {
      SPDLOG_INFO("CTP TD sent settlement info confirm, broker {} investor {} waiting for OnRspSettlementInfoConfirm",
                  req.BrokerID, req.InvestorID);
    }
    return false;
  });
}

void TraderCtp::on_rsp_error(const CThostFtdcRspInfoField &error, int request_id) {
  enqueue([error, request_id]() -> bool {
    SPDLOG_ERROR("CTP TD rsp error {}: {} (request {})", error.ErrorID, error.ErrorMsg, request_id);
    return false;
  });
}

// ==== order / trade callbacks ====

void TraderCtp::on_rtn_order(const CThostFtdcOrderField &order) {
  enqueue([this, order]() -> bool {
    Order kf_order = order_from_ctp(order);
    uint32_t source = resolve_order_source(kf_order.order_id);
    write_order(kf_order, source);
    if (is_final_status(kf_order.status)) {
      erase_order_mapping(kf_order.order_id);
    }
    return false;
  });
}

void TraderCtp::on_rtn_trade(const CThostFtdcTradeField &trade) {
  enqueue([this, trade]() -> bool {
    Trade kf_trade = trade_from_ctp(trade);
    uint32_t source = resolve_order_source(kf_trade.order_id);
    write_trade(kf_trade, source);
    return false;
  });
}

void TraderCtp::on_qry_order(const CThostFtdcOrderField &order, bool is_last) {
  enqueue([this, order, is_last]() -> bool {
    Order kf_order = order_from_ctp(order);
    uint32_t source = resolve_order_source(kf_order.order_id);
    write_order(kf_order, source);
    if (is_last) {
      finish_in_flight_query();
    }
    return false;
  });
}

void TraderCtp::on_qry_trade(const CThostFtdcTradeField &trade, bool is_last) {
  enqueue([this, trade, is_last]() -> bool {
    Trade kf_trade = trade_from_ctp(trade);
    uint32_t source = resolve_order_source(kf_trade.order_id);
    write_trade(kf_trade, source);
    if (is_last) {
      finish_in_flight_query();
    }
    return false;
  });
}

void TraderCtp::on_order_insert_rejected(const CThostFtdcInputOrderField &input, const CThostFtdcRspInfoField &error) {
  enqueue([this, input, error]() -> bool {
    std::string order_ref = input.OrderRef;
    auto ref_it = ref_to_order_id_.find(order_ref);
    if (ref_it == ref_to_order_id_.end()) {
      SPDLOG_ERROR("CTP TD order insert rejected for unknown ref {}, error {}: {}", order_ref, error.ErrorID,
                   error.ErrorMsg);
      return false;
    }
    uint64_t order_id = ref_it->second;

    Order order = {};
    order.order_id = order_id;
    strncpy(order.external_order_id, order_ref.c_str(), EXTERNAL_ID_LEN);
    strncpy(order.instrument_id, input.InstrumentID, INSTRUMENT_ID_LEN);
    strncpy(order.exchange_id, input.ExchangeID, EXCHANGE_ID_LEN);
    order.instrument_type = get_instrument_type(input.ExchangeID, input.InstrumentID);
    order.limit_price = input.LimitPrice;
    order.frozen_price = (input.OrderPriceType == THOST_FTDC_OPT_LimitPrice) ? input.LimitPrice : 0.0;
    order.volume = input.VolumeTotalOriginal;
    order.volume_left = 0;
    order.status = OrderStatus::Error;
    order.error_id = error.ErrorID;
    strncpy(order.error_msg, error.ErrorMsg, ERROR_MSG_LEN);
    order.side = side_from_ctp_direction(input.Direction);
    order.offset = offset_from_ctp(input.CombOffsetFlag[0]);
    order.hedge_flag = hedge_flag_from_ctp(input.CombHedgeFlag[0]);
    order.price_type = (input.OrderPriceType == THOST_FTDC_OPT_LimitPrice) ? PriceType::Limit : PriceType::Any;
    order.insert_time = yijinjing::time::now_in_nano();
    order.update_time = yijinjing::time::now_in_nano();

    uint32_t source = resolve_order_source(order_id);
    write_order(order, source);
    erase_order_mapping(order_id);
    return false;
  });
}

void TraderCtp::on_order_action_rejected(const CThostFtdcInputOrderActionField &action,
                                         const CThostFtdcRspInfoField &error) {
  enqueue([this, action, error]() -> bool {
    std::string order_ref = action.OrderRef;
    auto ref_it = ref_to_order_id_.find(order_ref);
    uint64_t order_id = ref_it != ref_to_order_id_.end() ? ref_it->second : 0;
    write_order_action_error(order_id, action.OrderActionRef, order_ref, error.ErrorID, error.ErrorMsg);
    return false;
  });
}

void TraderCtp::on_err_rtn_order_action(const CThostFtdcOrderActionField &action,
                                        const CThostFtdcRspInfoField &error) {
  enqueue([this, action, error]() -> bool {
    std::string order_ref = action.OrderRef;
    auto ref_it = ref_to_order_id_.find(order_ref);
    uint64_t order_id = ref_it != ref_to_order_id_.end() ? ref_it->second : 0;
    write_order_action_error(order_id, action.OrderActionRef, order_ref, error.ErrorID, error.ErrorMsg);
    return false;
  });
}

// ==== queries ====

void TraderCtp::on_qry_instrument(const CThostFtdcInstrumentField &instrument, bool is_last) {
  enqueue([this, instrument, is_last]() -> bool {
    if (instrument.VolumeMultiple > 0) {
      volume_multiples_[instrument.InstrumentID] = instrument.VolumeMultiple;
    }

    // 把合约乘数等基础字段写入 SYNC 通道（全量快照），cached 持久化后
    // ledger 启动时由 Bookkeeper::restore() 恢复，避免 future.hpp 拿不到
    // contract_multiplier 而走默认兜底值。保证金率字段暂留待后续补齐
    // （需另查 ReqQryInstrumentMarginRate 才有准确的 long/short_margin_ratio）。
    Instrument kf_instrument = {};
    strncpy(kf_instrument.instrument_id, instrument.InstrumentID, INSTRUMENT_ID_LEN);
    std::string exchange_id = resolve_exchange_id(instrument.ExchangeID, instrument.InstrumentID);
    strncpy(kf_instrument.exchange_id, exchange_id.c_str(), EXCHANGE_ID_LEN);
    kf_instrument.instrument_type = get_instrument_type(exchange_id, instrument.InstrumentID);
    kf_instrument.contract_multiplier = instrument.VolumeMultiple;

    if (auto writer = get_writer(yijinjing::data::location::SYNC)) {
      writer->write(yijinjing::time::now_in_nano(), kf_instrument);
    }

    if (is_last) {
      // CTP 最后一帧通常带数据 + bIsLast=true；on_qry_instrument_end 仅在
      // pInstrument==nullptr + bIsLast=true 时被调用，所以这里也要放行。
      finish_in_flight_query();
    }
    return false;
  });
}

void TraderCtp::on_qry_instrument_end() {
  enqueue([this]() -> bool {
    finish_in_flight_query();
    return false;
  });
}

void TraderCtp::on_qry_trading_account(const CThostFtdcTradingAccountField &account, bool is_last) {
  enqueue([this, account, is_last]() -> bool {
    Asset asset = {};
    asset.holder_uid = get_home_uid();
    asset.ledger_category = LedgerCategory::Account;
    asset.update_time = yijinjing::time::now_in_nano();
    strncpy(asset.trading_day, account.TradingDay, DATE_LEN);
    asset.initial_equity = account.PreBalance;
    asset.static_equity = account.PreBalance;
    asset.dynamic_equity = account.Balance;
    asset.realized_pnl = account.CloseProfit;
    asset.unrealized_pnl = account.PositionProfit;
    asset.avail = account.Available;
    asset.margin = account.CurrMargin;
    asset.accumulated_fee = account.Commission;
    asset.intraday_fee = account.Commission;
    asset.frozen_cash = account.FrozenMargin + account.FrozenCash + account.FrozenCommission;
    asset.frozen_margin = account.FrozenMargin;
    asset.frozen_fee = account.FrozenCommission;
    asset.position_pnl = account.PositionProfit;
    asset.close_pnl = account.CloseProfit;

    if (auto writer = get_asset_writer()) {
      writer->write(yijinjing::time::now_in_nano(), asset);
    }
    SPDLOG_INFO("CTP TD account synced, balance {:.2f}, available {:.2f}", account.Balance, account.Available);

    if (is_last) {
      finish_in_flight_query();
    }
    return false;
  });
}

void TraderCtp::on_qry_investor_position(const CThostFtdcInvestorPositionField &position, bool is_last) {
  enqueue([this, position, is_last]() -> bool {
    if (is_last) {
      // 在处理前先放行下一个查询：drain_tasks 与 pump_query_queue 同在主事件
      // 线程，下一个查询要等下次 1.1s tick 才发，那时本帧已处理完。
      finish_in_flight_query();
    }
    std::string instrument_id = position.InstrumentID;
    if (instrument_id.empty()) {
      return false;
    }
    std::string exchange_id = resolve_exchange_id(position.ExchangeID, instrument_id);
    Direction direction =
        (position.PosiDirection == THOST_FTDC_PD_Short) ? Direction::Short : Direction::Long;
    std::string key = instrument_id + "|" + (direction == Direction::Long ? "L" : "S");

    PositionRow &row = pending_positions_[key];
    Position &pos = row.position;
    if (pos.volume == 0 && pos.yesterday_volume == 0 && pos.margin == 0.0) {
      pos.holder_uid = get_home_uid();
      pos.ledger_category = LedgerCategory::Account;
      pos.direction = direction;
      strncpy(pos.instrument_id, instrument_id.c_str(), INSTRUMENT_ID_LEN);
      strncpy(pos.exchange_id, exchange_id.c_str(), EXCHANGE_ID_LEN);
      pos.instrument_type = get_instrument_type(exchange_id, instrument_id);
      strncpy(pos.trading_day, position.TradingDay, DATE_LEN);
      pos.pre_settlement_price = position.PreSettlementPrice;
      pos.settlement_price = position.SettlementPrice;
    }

    pos.volume += position.Position;
    pos.yesterday_volume += position.YdPosition;
    pos.frozen_total += (direction == Direction::Long) ? position.LongFrozen : position.ShortFrozen;
    pos.margin += position.UseMargin;
    pos.position_pnl += position.PositionProfit;
    pos.close_pnl += position.CloseProfit;
    row.open_amount += position.OpenAmount;
    row.open_volume += position.OpenVolume;
    row.open_cost += position.OpenCost;
    return false;
  });
}

void TraderCtp::on_qry_investor_position_end() {
  enqueue([this]() -> bool {
    flush_positions();
    finish_in_flight_query();
    return false;
  });
}

void TraderCtp::flush_positions() {
  if (pending_positions_.empty()) {
    SPDLOG_INFO("CTP TD position synced, 0 rows");
    if (auto writer = get_position_writer()) {
      PositionEnd end = {};
      end.holder_uid = get_home_uid();
      writer->write(yijinjing::time::now_in_nano(), end);
    }
    return;
  }

  int64_t nano = yijinjing::time::now_in_nano();
  auto writer = get_position_writer();
  size_t rows = pending_positions_.size();
  for (auto &pair : pending_positions_) {
    PositionRow &row = pair.second;
    Position &pos = row.position;
    int multiple = 1;
    auto multiple_it = volume_multiples_.find(pos.instrument_id.to_string());
    if (multiple_it != volume_multiples_.end() && multiple_it->second > 0) {
      multiple = multiple_it->second;
    }
    pos.update_time = nano;
    pos.avg_open_price = row.open_volume > 0 ? row.open_amount / row.open_volume / multiple : 0.0;
    pos.position_cost_price = pos.volume > 0 ? row.open_cost / pos.volume / multiple : 0.0;
    pos.realized_pnl = pos.close_pnl;
    if (writer) {
      writer->write(nano, pos);
    }
  }
  pending_positions_.clear();

  if (writer) {
    PositionEnd end = {};
    end.holder_uid = get_home_uid();
    writer->write(nano, end);
  }
  SPDLOG_INFO("CTP TD position synced, {} rows", rows);
}

// ==== request handlers ====

bool TraderCtp::insert_order(const event_ptr &event) {
  const OrderInput &input = event->data<OrderInput>();
  uint32_t source = event->source();

  if (!is_ready_for_requests()) {
    write_error_order(input, source, -1, "CTP TD not ready");
    return false;
  }

  char direction = ctp_direction_from_side(input.side);
  if (direction == '\0') {
    write_error_order(input, source, -1, fmt::format("unsupported side {} for CTP", (int)input.side));
    return false;
  }
  if (input.volume <= 0) {
    write_error_order(input, source, -1, "order volume must be positive");
    return false;
  }
  if (input.price_type == PriceType::Limit && input.limit_price <= 0.0) {
    write_error_order(input, source, -1, "limit order price must be positive");
    return false;
  }

  std::string instrument_id = input.instrument_id.to_string();
  std::string exchange_id = input.exchange_id.to_string();
  uint64_t order_ref = ++order_ref_counter_;
  std::string order_ref_str = fmt::format("{}", order_ref);

  CThostFtdcInputOrderField req = {};
  strncpy(req.BrokerID, config_.broker_id.c_str(), sizeof(req.BrokerID) - 1);
  strncpy(req.InvestorID, config_.investor_id.c_str(), sizeof(req.InvestorID) - 1);
  strncpy(req.UserID, config_.user_id.c_str(), sizeof(req.UserID) - 1);
  strncpy(req.InstrumentID, instrument_id.c_str(), sizeof(req.InstrumentID) - 1);
  strncpy(req.ExchangeID, exchange_id.c_str(), sizeof(req.ExchangeID) - 1);
  strncpy(req.OrderRef, order_ref_str.c_str(), sizeof(req.OrderRef) - 1);
  req.OrderPriceType = ctp_order_price_type_from(input.price_type);
  req.Direction = direction;
  req.CombOffsetFlag[0] = ctp_offset_from_offset(input.offset);
  req.CombHedgeFlag[0] = ctp_hedge_flag_from(input.hedge_flag);
  req.LimitPrice = input.limit_price;
  req.VolumeTotalOriginal = static_cast<int>(input.volume);
  req.TimeCondition = ctp_time_condition_from(input.price_type, input.time_condition);
  req.VolumeCondition = ctp_volume_condition_from(input.price_type, input.volume_condition);
  req.MinVolume = 1;
  req.ContingentCondition = THOST_FTDC_CC_Immediately;
  req.StopPrice = 0;
  req.ForceCloseReason = THOST_FTDC_FCC_NotForceClose;
  req.IsAutoSuspend = 0;

  int ret = api_->ReqOrderInsert(&req, ++request_id_);
  if (ret != 0) {
    write_error_order(input, source, ret, fmt::format("ReqOrderInsert failed, ret {}", ret));
    return false;
  }

  order_id_to_ref_info_[input.order_id] = {order_ref_str, instrument_id, exchange_id, source};
  ref_to_order_id_[order_ref_str] = input.order_id;

  SPDLOG_INFO("CTP TD order submitted, order_id {:x}, ref {}, {} {} {} {} @{} x {}", input.order_id, order_ref_str,
              exchange_id, instrument_id, (int)input.side, (int)input.offset, input.limit_price, input.volume);
  return true;
}

bool TraderCtp::cancel_order(const event_ptr &event) {
  const OrderAction &action = event->data<OrderAction>();

  auto info_it = order_id_to_ref_info_.find(action.order_id);
  if (info_it == order_id_to_ref_info_.end()) {
    write_order_action_error(action.order_id, action.order_action_id, "", -1, "order not found or already finished");
    return false;
  }
  if (!is_ready_for_requests()) {
    write_order_action_error(action.order_id, action.order_action_id, info_it->second.order_ref, -1,
                             "CTP TD not ready");
    return false;
  }

  const OrderRefInfo &info = info_it->second;
  CThostFtdcInputOrderActionField req = {};
  strncpy(req.BrokerID, config_.broker_id.c_str(), sizeof(req.BrokerID) - 1);
  strncpy(req.InvestorID, config_.investor_id.c_str(), sizeof(req.InvestorID) - 1);
  strncpy(req.InstrumentID, info.instrument_id.c_str(), sizeof(req.InstrumentID) - 1);
  strncpy(req.ExchangeID, info.exchange_id.c_str(), sizeof(req.ExchangeID) - 1);
  strncpy(req.OrderRef, info.order_ref.c_str(), sizeof(req.OrderRef) - 1);
  req.FrontID = front_id_;
  req.SessionID = session_id_;
  req.OrderActionRef = ++order_ref_counter_;
  req.ActionFlag = THOST_FTDC_AF_Delete;

  int ret = api_->ReqOrderAction(&req, ++request_id_);
  if (ret != 0) {
    write_order_action_error(action.order_id, action.order_action_id, info.order_ref, ret,
                             fmt::format("ReqOrderAction failed, ret {}", ret));
    return false;
  }
  SPDLOG_INFO("CTP TD cancel submitted, order_id {:x}, ref {}", action.order_id, info.order_ref);
  return true;
}

bool TraderCtp::req_position() {
  push_query([this]() -> bool {
    CThostFtdcQryInvestorPositionField req = {};
    strncpy(req.BrokerID, config_.broker_id.c_str(), sizeof(req.BrokerID) - 1);
    strncpy(req.InvestorID, config_.investor_id.c_str(), sizeof(req.InvestorID) - 1);
    int ret = api_->ReqQryInvestorPosition(&req, ++request_id_);
    if (ret != 0) {
      SPDLOG_ERROR("failed to send qry investor position, error {}", ret);
      return false;
    }
    return true;
  });
  return true;
}

bool TraderCtp::req_account() {
  push_query([this]() -> bool {
    CThostFtdcQryTradingAccountField req = {};
    strncpy(req.BrokerID, config_.broker_id.c_str(), sizeof(req.BrokerID) - 1);
    strncpy(req.InvestorID, config_.investor_id.c_str(), sizeof(req.InvestorID) - 1);
    int ret = api_->ReqQryTradingAccount(&req, ++request_id_);
    if (ret != 0) {
      SPDLOG_ERROR("failed to send qry trading account, error {}", ret);
      return false;
    }
    return true;
  });
  return true;
}

bool TraderCtp::req_order_trade() {
  push_query([this]() -> bool {
    CThostFtdcQryOrderField req = {};
    strncpy(req.InsertTimeStart, "00:00:00", sizeof(req.InsertTimeStart) - 1);
    strncpy(req.InsertTimeEnd, "23:59:59", sizeof(req.InsertTimeEnd) - 1);
    int ret = api_->ReqQryOrder(&req, ++request_id_);
    if (ret != 0) {
      SPDLOG_ERROR("failed to send qry order, error {}", ret);
      return false;
    }
    return true;
  });
  push_query([this]() -> bool {
    CThostFtdcQryTradeField req = {};
    strncpy(req.BrokerID, config_.broker_id.c_str(), sizeof(req.BrokerID) - 1);
    strncpy(req.InvestorID, config_.investor_id.c_str(), sizeof(req.InvestorID) - 1);
    int ret = api_->ReqQryTrade(&req, ++request_id_);
    if (ret != 0) {
      SPDLOG_ERROR("failed to send qry trade, error {}", ret);
      return false;
    }
    return true;
  });
  return true;
}

// ==== helpers ====

uint64_t TraderCtp::resolve_order_id(int front_id, int session_id, const std::string &exchange_id,
                                     const std::string &order_sys_id, const std::string &order_ref) const {
  if (front_id == front_id_ && session_id == session_id_ && !order_ref.empty()) {
    auto it = ref_to_order_id_.find(order_ref);
    if (it != ref_to_order_id_.end()) {
      return it->second;
    }
  }
  if (!order_sys_id.empty() && order_sys_id != "0") {
    return stable_hash64(fmt::format("syssid|{}|{}", exchange_id, order_sys_id));
  }
  return stable_hash64(fmt::format("ref|{}|{}|{}", front_id, session_id, order_ref));
}

uint32_t TraderCtp::resolve_order_source(uint64_t order_id) const {
  auto it = order_id_to_ref_info_.find(order_id);
  if (it != order_id_to_ref_info_.end() && it->second.source != 0) {
    return it->second.source;
  }
  return yijinjing::data::location::PUBLIC;
}

Order TraderCtp::order_from_ctp(const CThostFtdcOrderField &order) const {
  std::string order_ref = order.OrderRef;
  std::string instrument_id = order.InstrumentID;
  std::string exchange_id = resolve_exchange_id(order.ExchangeID, instrument_id);

  Order kf_order = {};
  kf_order.order_id = resolve_order_id(order.FrontID, order.SessionID, exchange_id, order.OrderSysID, order_ref);
  strncpy(kf_order.external_order_id, order_ref.c_str(), EXTERNAL_ID_LEN);
  strncpy(kf_order.trading_day, order.TradingDay, DATE_LEN);
  strncpy(kf_order.instrument_id, instrument_id.c_str(), INSTRUMENT_ID_LEN);
  strncpy(kf_order.exchange_id, exchange_id.c_str(), EXCHANGE_ID_LEN);
  kf_order.instrument_type = get_instrument_type(exchange_id, instrument_id);
  kf_order.limit_price = order.LimitPrice;
  kf_order.frozen_price = (order.OrderPriceType == THOST_FTDC_OPT_LimitPrice) ? order.LimitPrice : 0.0;
  kf_order.volume = order.VolumeTotalOriginal;
  kf_order.volume_left = order.VolumeTotal;
  kf_order.status = order_status_from_ctp(order.OrderStatus);
  kf_order.side = side_from_ctp_direction(order.Direction);
  kf_order.offset = offset_from_ctp(order.CombOffsetFlag[0]);
  kf_order.hedge_flag = hedge_flag_from_ctp(order.CombHedgeFlag[0]);
  kf_order.price_type = (order.OrderPriceType == THOST_FTDC_OPT_LimitPrice) ? PriceType::Limit : PriceType::Any;
  kf_order.time_condition = time_condition_from_ctp(order.TimeCondition);
  kf_order.volume_condition = volume_condition_from_ctp(order.VolumeCondition);
  kf_order.insert_time = nano_from_ctp_time(order.InsertDate, order.InsertTime, 0);
  kf_order.update_time = yijinjing::time::now_in_nano();
  if (kf_order.status == OrderStatus::Error) {
    kf_order.error_id = -1;
    strncpy(kf_order.error_msg, order.StatusMsg, ERROR_MSG_LEN);
  }
  return kf_order;
}

Trade TraderCtp::trade_from_ctp(const CThostFtdcTradeField &trade) const {
  std::string order_ref = trade.OrderRef;
  std::string instrument_id = trade.InstrumentID;
  std::string exchange_id = resolve_exchange_id(trade.ExchangeID, instrument_id);
  std::string order_sys_id = trade.OrderSysID;

  Trade kf_trade = {};
  // CThostFtdcTradeField carries no FrontID/SessionID, so resolve the order id
  // through the OrderRef map first, falling back to the OrderSysID hash that
  // matches resolve_order_id's synthetic branch.
  auto ref_it = ref_to_order_id_.find(order_ref);
  if (ref_it != ref_to_order_id_.end()) {
    kf_trade.order_id = ref_it->second;
  } else if (!order_sys_id.empty() && order_sys_id != "0") {
    kf_trade.order_id = stable_hash64(fmt::format("syssid|{}|{}", exchange_id, order_sys_id));
  } else {
    kf_trade.order_id = stable_hash64(fmt::format("trade|{}|{}", exchange_id, trade.TradeID));
  }

  kf_trade.trade_id = stable_hash64(fmt::format("trade|{}|{}", exchange_id, trade.TradeID));
  strncpy(kf_trade.external_order_id, order_ref.c_str(), EXTERNAL_ID_LEN);
  strncpy(kf_trade.external_trade_id, trade.TradeID, EXTERNAL_ID_LEN);
  kf_trade.trade_time = nano_from_ctp_time(trade.TradingDay, trade.TradeTime, 0);
  strncpy(kf_trade.trading_day, trade.TradingDay, DATE_LEN);
  strncpy(kf_trade.instrument_id, instrument_id.c_str(), INSTRUMENT_ID_LEN);
  strncpy(kf_trade.exchange_id, exchange_id.c_str(), EXCHANGE_ID_LEN);
  kf_trade.instrument_type = get_instrument_type(exchange_id, instrument_id);
  kf_trade.side = side_from_ctp_direction(trade.Direction);
  kf_trade.offset = offset_from_ctp(trade.OffsetFlag);
  kf_trade.hedge_flag = hedge_flag_from_ctp(trade.HedgeFlag);
  kf_trade.price = trade.Price;
  kf_trade.volume = trade.Volume;
  return kf_trade;
}

void TraderCtp::write_order(const Order &order, uint32_t source) {
  int64_t nano = yijinjing::time::now_in_nano();
  if (source != yijinjing::data::location::PUBLIC && has_writer(source)) {
    get_writer(source)->write(nano, order);
  }
  if (auto writer = get_writer(yijinjing::data::location::PUBLIC)) {
    writer->write(nano, order);
  }
}

void TraderCtp::write_trade(const Trade &trade, uint32_t source) {
  int64_t nano = yijinjing::time::now_in_nano();
  if (source != yijinjing::data::location::PUBLIC && has_writer(source)) {
    get_writer(source)->write(nano, trade);
  }
  if (auto writer = get_writer(yijinjing::data::location::PUBLIC)) {
    writer->write(nano, trade);
  }
}

void TraderCtp::write_error_order(const OrderInput &input, uint32_t source, int32_t error_id,
                                  const std::string &error_msg) {
  Order order = {};
  order_from_input(input, order);
  order.status = OrderStatus::Error;
  order.error_id = error_id;
  strncpy(order.error_msg, error_msg.c_str(), ERROR_MSG_LEN);
  order.insert_time = yijinjing::time::now_in_nano();
  order.update_time = order.insert_time;
  SPDLOG_ERROR("CTP TD order rejected, order_id {:x}, error {}: {}", input.order_id, error_id, error_msg);
  write_order(order, source);
}

void TraderCtp::write_order_action_error(uint64_t order_id, uint64_t order_action_id,
                                         const std::string &external_order_id, int32_t error_id,
                                         const std::string &error_msg) {
  OrderActionError error = {};
  error.order_id = order_id;
  error.order_action_id = order_action_id;
  error.error_id = error_id;
  strncpy(error.external_order_id, external_order_id.c_str(), EXTERNAL_ID_LEN);
  strncpy(error.error_msg, error_msg.c_str(), ERROR_MSG_LEN);
  error.insert_time = yijinjing::time::now_in_nano();

  int64_t nano = yijinjing::time::now_in_nano();
  if (auto writer = get_writer(yijinjing::data::location::PUBLIC)) {
    writer->write(nano, error);
  }
  SPDLOG_ERROR("CTP TD cancel rejected, order_id {:x}, error {}: {}", order_id, error_id, error_msg);
}

void TraderCtp::erase_order_mapping(uint64_t order_id) {
  auto info_it = order_id_to_ref_info_.find(order_id);
  if (info_it != order_id_to_ref_info_.end()) {
    ref_to_order_id_.erase(info_it->second.order_ref);
    order_id_to_ref_info_.erase(info_it);
  }
}

} // namespace kungfu::wingchun::ctp
