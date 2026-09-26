#pragma once

#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <kungfu/wingchun/broker/trader.h>
#include <ThostFtdcTraderApi.h>

#include "ctp_common.h"

namespace kungfu::wingchun::ctp {

class CtpTraderSpi;

class TraderCtp : public broker::Trader {
public:
  explicit TraderCtp(broker::TraderVendor &vendor);

  ~TraderCtp() override;

  [[nodiscard]] longfist::enums::AccountType get_account_type() const override;

  void on_start() override;

  void on_exit() override;

  /// Rebuild OrderRef -> order_id mappings from today's recovered orders so
  /// async CTP callbacks can be correlated with kungfu order ids after a
  /// restart within the same trading day.
  void on_recover() override;

  bool insert_order(const event_ptr &event) override;

  bool cancel_order(const event_ptr &event) override;

  bool req_position() override;

  bool req_account() override;

  bool req_order_trade() override;

  // ==== CTP SPI callbacks (CTP worker thread, enqueue only) ====

  void on_front_disconnected(int reason);

  void on_login_success(const CThostFtdcRspUserLoginField &login);

  void on_login_failed(const CThostFtdcRspInfoField &error);

  /// 查询投资者结算结果确认响应：决定是否需要再次发起确认。已确认（当日）
  /// 则直接进入 Ready，否则拉取结算单后调用 ReqSettlementInfoConfirm。
  void on_qry_settlement_info_confirm(const CThostFtdcSettlementInfoConfirmField *confirm, bool is_last);

  /// 查询投资者结算结果响应：分页累积，bIsLast 时发起确认请求。
  void on_qry_settlement_info(const CThostFtdcSettlementInfoField *info, bool is_last);

  void on_settlement_confirmed();

  void on_rsp_error(const CThostFtdcRspInfoField &error, int request_id);

  void on_order_insert_rejected(const CThostFtdcInputOrderField &input, const CThostFtdcRspInfoField &error);

  void on_order_action_rejected(const CThostFtdcInputOrderActionField &action, const CThostFtdcRspInfoField &error);

  void on_err_rtn_order_action(const CThostFtdcOrderActionField &action, const CThostFtdcRspInfoField &error);

  void on_rtn_order(const CThostFtdcOrderField &order);

  void on_rtn_trade(const CThostFtdcTradeField &trade);

  void on_qry_instrument(const CThostFtdcInstrumentField &instrument, bool is_last);

  void on_qry_instrument_end();

  void on_qry_trading_account(const CThostFtdcTradingAccountField &account, bool is_last);

  void on_qry_investor_position(const CThostFtdcInvestorPositionField &position, bool is_last);

  void on_qry_investor_position_end();

  void on_qry_order(const CThostFtdcOrderField &order, bool is_last);

  void on_qry_trade(const CThostFtdcTradeField &trade, bool is_last);

private:
  // A queued query returns true if it was successfully submitted to CTP and
  // is now waiting for its bIsLast/end callback to clear query_in_flight_.
  // Returns false for placeholder / send-failure cases where there is no
  // callback to wait for; pump_query_queue clears in_flight immediately.
  using Task = std::function<bool()>;

  /// Minimal per-order info kept to translate kungfu order ids back to CTP
  /// OrderRef / instrument pairs (needed for cancels).
  struct OrderRefInfo {
    std::string order_ref;
    std::string instrument_id;
    std::string exchange_id;
    uint32_t source = 0;
  };

  /// ReqQryInvestorPosition rows arrive page-wise and may split one
  /// (instrument, direction) position into several records; accumulate the
  /// raw CTP amounts until bIsLast, then convert to longfist Position.
  struct PositionRow {
    longfist::types::Position position = {};
    double open_amount = 0.0;
    int64_t open_volume = 0;
    double open_cost = 0.0;
  };

  // ==== event loop helpers ====
  void enqueue(Task task);

  void drain_tasks();

  /// Executes at most one queued query per tick to respect the CTP
  /// "one query per second" flow control. A query stays in-flight until its
  /// bIsLast/end callback (or a send-failure path) calls finish_in_flight_query,
  /// so CTP never sees overlapping queries (which return error -2).
  void pump_query_queue();

  void push_query(Task query);

  /// Marks the current in-flight query as finished and lets the next pump
  /// tick pick up the following query. Called from CTP SPI end callbacks
  /// (is_last=true / on_qry_xxx_end) and from send-failure paths.
  void finish_in_flight_query();

  bool is_ready_for_requests() const;

  // ==== order related helpers (event loop) ====

  /// Maps a CTP order notification to a kungfu order id: own-session refs map
  /// to the order id from OrderInput; anything else gets a stable synthetic id
  /// derived from the exchange OrderSysID (or front/session/ref as fallback).
  [[nodiscard]] uint64_t resolve_order_id(int front_id, int session_id, const std::string &exchange_id,
                                          const std::string &order_sys_id, const std::string &order_ref) const;

  [[nodiscard]] uint32_t resolve_order_source(uint64_t order_id) const;

  void write_order(const longfist::types::Order &order, uint32_t source);

  void write_trade(const longfist::types::Trade &trade, uint32_t source);

  void write_error_order(const longfist::types::OrderInput &input, uint32_t source, int32_t error_id,
                         const std::string &error_msg);

  void write_order_action_error(uint64_t order_id, uint64_t order_action_id, const std::string &external_order_id,
                                int32_t error_id, const std::string &error_msg);

  [[nodiscard]] longfist::types::Order order_from_ctp(const CThostFtdcOrderField &order) const;

  [[nodiscard]] longfist::types::Trade trade_from_ctp(const CThostFtdcTradeField &trade) const;

  void flush_positions();

  void erase_order_mapping(uint64_t order_id);

  CThostFtdcTraderApi *api_ = nullptr;
  CtpTraderSpi *spi_ = nullptr;
  CtpConfig config_;
  int request_id_ = 0;

  // All members below are only touched on the kungfu event loop thread.
  bool logged_in_ = false;
  bool ready_ = false;
  int front_id_ = 0;
  int session_id_ = 0;
  std::string trading_day_;
  uint64_t order_ref_counter_ = 0;

  std::unordered_map<uint64_t, OrderRefInfo> order_id_to_ref_info_;
  std::unordered_map<std::string, uint64_t> ref_to_order_id_;

  std::unordered_map<std::string, int> volume_multiples_;

  // CTP throttles queries; queue them and send at most one per interval.
  std::deque<Task> query_queue_;

  // True while a queued query is waiting for its CTP bIsLast/end callback.
  // pump_query_queue skips a tick when this is set, so queries never overlap.
  bool query_in_flight_ = false;

  // Wall-clock nanos when the in-flight query was submitted. Used by
  // pump_query_queue to force-reset query_in_flight_ after QUERY_TIMEOUT_NS
  // so a lost callback (or a stuck CTP session) cannot permanently stall
  // the queue. 120s covers instrument paging (~30s) plus margin.
  int64_t query_started_at_nano_ = 0;
  static constexpr int64_t QUERY_TIMEOUT_NS = 120LL * 1000 * 1000 * 1000;

  // Pending position rows accumulated during ReqQryInvestorPosition paging.
  std::map<std::string, PositionRow> pending_positions_;

  std::deque<Task> tasks_;
  std::mutex tasks_mutex_;
};

} // namespace kungfu::wingchun::ctp
