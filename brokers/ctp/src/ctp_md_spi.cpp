#include "ctp_md_spi.h"

#include <spdlog/spdlog.h>

#include "market_data_ctp.h"

namespace kungfu::wingchun::ctp {

CtpMdSpi::CtpMdSpi(CThostFtdcMdApi *api, MarketDataCtp *service, CtpConfig config)
    : api_(api), service_(service), config_(std::move(config)) {}

void CtpMdSpi::OnFrontConnected() {
  SPDLOG_INFO("CTP MD connected to front {}", config_.front_uri);
  CThostFtdcReqUserLoginField req = {};
  strncpy(req.BrokerID, config_.broker_id.c_str(), sizeof(req.BrokerID) - 1);
  strncpy(req.UserID, config_.user_id.c_str(), sizeof(req.UserID) - 1);
  strncpy(req.Password, config_.password.c_str(), sizeof(req.Password) - 1);
  int ret = api_->ReqUserLogin(&req, ++request_id_);
  if (ret != 0) {
    SPDLOG_ERROR("failed to send CTP MD login request, error {}", ret);
  }
}

void CtpMdSpi::OnFrontDisconnected(int nReason) {
  SPDLOG_WARN("CTP MD front disconnected, reason 0x{:x}", nReason);
  service_->on_front_disconnected(nReason);
}

void CtpMdSpi::OnHeartBeatWarning(int nTimeLapse) {
  SPDLOG_WARN("CTP MD heartbeat warning, {}s since last message", nTimeLapse);
}

void CtpMdSpi::OnRspUserLogin(CThostFtdcRspUserLoginField *pRspUserLogin, CThostFtdcRspInfoField *pRspInfo,
                              int nRequestID, bool bIsLast) {
  if (pRspInfo && pRspInfo->ErrorID != 0) {
    SPDLOG_ERROR("CTP MD login failed, error {}: {}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    service_->on_login_failed(*pRspInfo);
    return;
  }
  if (pRspUserLogin == nullptr) {
    SPDLOG_ERROR("CTP MD login response has no data");
    return;
  }
  SPDLOG_INFO("CTP MD login success, trading day {}, login time {}", pRspUserLogin->TradingDay,
              pRspUserLogin->LoginTime);
  service_->on_login_success(*pRspUserLogin);
}

void CtpMdSpi::OnRspUserLogout(CThostFtdcUserLogoutField *pUserLogout, CThostFtdcRspInfoField *pRspInfo, int nRequestID,
                               bool bIsLast) {
  if (pRspInfo && pRspInfo->ErrorID != 0) {
    SPDLOG_ERROR("CTP MD logout failed, error {}: {}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
  } else {
    SPDLOG_INFO("CTP MD logout success");
    service_->on_front_disconnected(0);
  }
}

void CtpMdSpi::OnRspError(CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
  if (pRspInfo) {
    SPDLOG_ERROR("CTP MD error response, error {}: {}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
  }
}

void CtpMdSpi::OnRspSubMarketData(CThostFtdcSpecificInstrumentField *pSpecificInstrument, CThostFtdcRspInfoField *pRspInfo,
                                  int nRequestID, bool bIsLast) {
  if (pRspInfo && pRspInfo->ErrorID != 0) {
    SPDLOG_ERROR("CTP MD subscribe failed, error {}: {}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
  } else if (pSpecificInstrument) {
    SPDLOG_INFO("CTP MD subscribed {}", pSpecificInstrument->InstrumentID);
  }
}

void CtpMdSpi::OnRspUnSubMarketData(CThostFtdcSpecificInstrumentField *pSpecificInstrument,
                                    CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
  if (pRspInfo && pRspInfo->ErrorID != 0) {
    SPDLOG_ERROR("CTP MD unsubscribe failed, error {}: {}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
  } else if (pSpecificInstrument) {
    SPDLOG_INFO("CTP MD unsubscribed {}", pSpecificInstrument->InstrumentID);
  }
}

void CtpMdSpi::OnRtnDepthMarketData(CThostFtdcDepthMarketDataField *pDepthMarketData) {
  if (pDepthMarketData) {
    service_->on_depth_market_data(*pDepthMarketData);
  }
}

} // namespace kungfu::wingchun::ctp
