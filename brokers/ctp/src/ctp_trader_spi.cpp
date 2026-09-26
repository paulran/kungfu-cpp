#include "ctp_trader_spi.h"

#include <spdlog/spdlog.h>

#include "trader_ctp.h"

namespace kungfu::wingchun::ctp {

CtpTraderSpi::CtpTraderSpi(CThostFtdcTraderApi *api, TraderCtp *service, CtpConfig config)
    : api_(api), service_(service), config_(std::move(config)) {}

void CtpTraderSpi::OnFrontConnected() {
  SPDLOG_INFO("CTP TD connected to front {}", config_.front_uri);
  CThostFtdcReqAuthenticateField req = {0};
  strncpy(req.BrokerID, config_.broker_id.c_str(), sizeof(req.BrokerID) - 1);
  strncpy(req.UserID, config_.user_id.c_str(), sizeof(req.UserID) - 1);
  strncpy(req.UserProductInfo, config_.user_product_info.c_str(), sizeof(req.UserProductInfo) - 1);
  strncpy(req.AuthCode, config_.auth_code.c_str(), sizeof(req.AuthCode) - 1);
  strncpy(req.AppID, config_.app_id.c_str(), sizeof(req.AppID) - 1);
  int ret = api_->ReqAuthenticate(&req, ++request_id_);
  if (ret != 0) {
    SPDLOG_ERROR("failed to send CTP TD authenticate request, error {}", ret);
  }
}

void CtpTraderSpi::OnFrontDisconnected(int nReason) {
  SPDLOG_WARN("CTP TD front disconnected, reason 0x{:x}", nReason);
  service_->on_front_disconnected(nReason);
}

void CtpTraderSpi::OnHeartBeatWarning(int nTimeLapse) {
  SPDLOG_WARN("CTP TD heartbeat warning, {}s since last message", nTimeLapse);
}

void CtpTraderSpi::OnRspAuthenticate(CThostFtdcRspAuthenticateField *pRspAuthenticateField,
                                     CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
  if (pRspInfo && pRspInfo->ErrorID != 0) {
    SPDLOG_ERROR("CTP TD authentication failed, error {}: {}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    service_->on_login_failed(*pRspInfo);
    return;
  }
  SPDLOG_INFO("CTP TD authentication success");
  CThostFtdcReqUserLoginField req = {0};
  strncpy(req.BrokerID, config_.broker_id.c_str(), sizeof(req.BrokerID) - 1);
  strncpy(req.UserID, config_.user_id.c_str(), sizeof(req.UserID) - 1);
  strncpy(req.Password, config_.password.c_str(), sizeof(req.Password) - 1);
  int ret = api_->ReqUserLogin(&req, ++request_id_);
  if (ret != 0) {
    SPDLOG_ERROR("failed to send CTP TD login request, error {}", ret);
  }
}

void CtpTraderSpi::OnRspUserLogin(CThostFtdcRspUserLoginField *pRspUserLogin, CThostFtdcRspInfoField *pRspInfo,
                                  int nRequestID, bool bIsLast) {
  if (pRspInfo && pRspInfo->ErrorID != 0) {
    SPDLOG_ERROR("CTP TD login failed, error {}: {}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    service_->on_login_failed(*pRspInfo);
    return;
  }
  if (pRspUserLogin == nullptr) {
    SPDLOG_ERROR("CTP TD login response has no data");
    return;
  }
  SPDLOG_INFO("CTP TD login success, trading day {}, login time {}, front {}, session {}", pRspUserLogin->TradingDay,
              pRspUserLogin->LoginTime, pRspUserLogin->FrontID, pRspUserLogin->SessionID);
  service_->on_login_success(*pRspUserLogin);
}

void CtpTraderSpi::OnRspUserLogout(CThostFtdcUserLogoutField *pUserLogout, CThostFtdcRspInfoField *pRspInfo,
                                   int nRequestID, bool bIsLast) {
  if (pRspInfo && pRspInfo->ErrorID != 0) {
    SPDLOG_ERROR("CTP TD logout failed, error {}: {}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
  } else {
    SPDLOG_INFO("CTP TD logout success");
    service_->on_front_disconnected(0);
  }
}

void CtpTraderSpi::OnRspError(CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
  if (pRspInfo) {
    SPDLOG_ERROR("CTP TD error response, error {}: {}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    service_->on_rsp_error(*pRspInfo, nRequestID);
  }
}

void CtpTraderSpi::OnRspOrderInsert(CThostFtdcInputOrderField *pInputOrder, CThostFtdcRspInfoField *pRspInfo,
                                    int nRequestID, bool bIsLast) {
  if (pRspInfo && pRspInfo->ErrorID != 0) {
    SPDLOG_ERROR("CTP TD order insert rejected by front, error {}: {}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    if (pInputOrder) {
      service_->on_order_insert_rejected(*pInputOrder, *pRspInfo);
    }
  }
}

void CtpTraderSpi::OnRspOrderAction(CThostFtdcInputOrderActionField *pInputOrderAction, CThostFtdcRspInfoField *pRspInfo,
                                    int nRequestID, bool bIsLast) {
  if (pRspInfo && pRspInfo->ErrorID != 0) {
    SPDLOG_ERROR("CTP TD order action rejected by front, error {}: {}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    if (pInputOrderAction) {
      service_->on_order_action_rejected(*pInputOrderAction, *pRspInfo);
    }
  }
}

void CtpTraderSpi::OnRspSettlementInfoConfirm(CThostFtdcSettlementInfoConfirmField *pSettlementInfoConfirm,
                                              CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
  if (pRspInfo && pRspInfo->ErrorID != 0) {
    SPDLOG_ERROR("CTP TD settlement confirm failed, error {}: {}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    return;
  }
  SPDLOG_INFO("CTP TD settlement info confirmed");
  service_->on_settlement_confirmed();
}

void CtpTraderSpi::OnRspQrySettlementInfoConfirm(CThostFtdcSettlementInfoConfirmField *pSettlementInfoConfirm,
                                                CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
  if (pRspInfo && pRspInfo->ErrorID != 0) {
    SPDLOG_ERROR("CTP TD qry settlement info confirm failed, error {}: {}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    return;
  }
  service_->on_qry_settlement_info_confirm(pSettlementInfoConfirm, bIsLast);
}

void CtpTraderSpi::OnRspQrySettlementInfo(CThostFtdcSettlementInfoField *pSettlementInfo,
                                          CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
  if (pRspInfo && pRspInfo->ErrorID != 0) {
    SPDLOG_ERROR("CTP TD qry settlement info failed, error {}: {}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    return;
  }
  service_->on_qry_settlement_info(pSettlementInfo, bIsLast);
}

void CtpTraderSpi::OnRspQryInstrument(CThostFtdcInstrumentField *pInstrument, CThostFtdcRspInfoField *pRspInfo,
                                      int nRequestID, bool bIsLast) {
  if (pRspInfo && pRspInfo->ErrorID != 0) {
    SPDLOG_ERROR("CTP TD qry instrument failed, error {}: {}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    return;
  }
  // SPDLOG_INFO("CTP TD qry instrument, instrument {}, last {}", pInstrument->InstrumentID, bIsLast);
  
  if (pInstrument) {
    service_->on_qry_instrument(*pInstrument, bIsLast);
  }

  if (bIsLast) {
    SPDLOG_INFO("CTP TD qry instrument end");
    service_->on_qry_instrument_end();
  }
}

void CtpTraderSpi::OnRspQryTradingAccount(CThostFtdcTradingAccountField *pTradingAccount,
                                          CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
  if (pRspInfo && pRspInfo->ErrorID != 0) {
    SPDLOG_ERROR("CTP TD qry trading account failed, error {}: {}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    return;
  }
  if (pTradingAccount) {
    service_->on_qry_trading_account(*pTradingAccount, bIsLast);
  }
}

void CtpTraderSpi::OnRspQryInvestorPosition(CThostFtdcInvestorPositionField *pInvestorPosition,
                                            CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
  if (pRspInfo && pRspInfo->ErrorID != 0) {
    SPDLOG_ERROR("CTP TD qry investor position failed, error {}: {}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    return;
  }
  if (pInvestorPosition) {
    service_->on_qry_investor_position(*pInvestorPosition, bIsLast);
  } else if (bIsLast) {
    service_->on_qry_investor_position_end();
  }
}

void CtpTraderSpi::OnRspQryOrder(CThostFtdcOrderField *pOrder, CThostFtdcRspInfoField *pRspInfo, int nRequestID,
                                 bool bIsLast) {
  if (pRspInfo && pRspInfo->ErrorID != 0) {
    SPDLOG_ERROR("CTP TD qry order failed, error {}: {}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    return;
  }
  if (pOrder) {
    service_->on_qry_order(*pOrder, bIsLast);
  }
}

void CtpTraderSpi::OnRspQryTrade(CThostFtdcTradeField *pTrade, CThostFtdcRspInfoField *pRspInfo, int nRequestID,
                                 bool bIsLast) {
  if (pRspInfo && pRspInfo->ErrorID != 0) {
    SPDLOG_ERROR("CTP TD qry trade failed, error {}: {}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    return;
  }
  if (pTrade) {
    service_->on_qry_trade(*pTrade, bIsLast);
  }
}

void CtpTraderSpi::OnRtnOrder(CThostFtdcOrderField *pOrder) {
  if (pOrder) {
    service_->on_rtn_order(*pOrder);
  }
}

void CtpTraderSpi::OnRtnTrade(CThostFtdcTradeField *pTrade) {
  if (pTrade) {
    service_->on_rtn_trade(*pTrade);
  }
}

void CtpTraderSpi::OnErrRtnOrderInsert(CThostFtdcInputOrderField *pInputOrder, CThostFtdcRspInfoField *pRspInfo) {
  if (pRspInfo) {
    SPDLOG_ERROR("CTP TD order insert rejected by exchange, error {}: {}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
  }
  if (pInputOrder) {
    CThostFtdcRspInfoField fallback = {};
    if (pRspInfo == nullptr) {
      fallback.ErrorID = -1;
      strncpy(fallback.ErrorMsg, "order insert rejected by exchange", sizeof(fallback.ErrorMsg) - 1);
      service_->on_order_insert_rejected(*pInputOrder, fallback);
    } else {
      service_->on_order_insert_rejected(*pInputOrder, *pRspInfo);
    }
  }
}

void CtpTraderSpi::OnErrRtnOrderAction(CThostFtdcOrderActionField *pOrderAction, CThostFtdcRspInfoField *pRspInfo) {
  if (pRspInfo) {
    SPDLOG_ERROR("CTP TD order action rejected, error {}: {}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
  }
  if (pOrderAction && pRspInfo) {
    service_->on_err_rtn_order_action(*pOrderAction, *pRspInfo);
  }
}

} // namespace kungfu::wingchun::ctp
