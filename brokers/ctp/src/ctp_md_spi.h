#pragma once

// CTP market data front SPI. Runs on the CTP API worker thread; every
// callback only converts its data and enqueues a task on the owning
// MarketDataCtp service, which executes it on the kungfu event loop thread
// (journal writers must only be touched from the event loop).

#include <ThostFtdcMdApi.h>

#include "ctp_common.h"

namespace kungfu::wingchun::ctp {

class MarketDataCtp;

class CtpMdSpi : public CThostFtdcMdSpi {
public:
  CtpMdSpi(CThostFtdcMdApi *api, MarketDataCtp *service, CtpConfig config);

  /// 当客户端与交易后台建立起通信连接时（还未登录前），该方法被调用。
  void OnFrontConnected() override;

  /// 当客户端与交易后台通信连接断开时，该方法被调用。API会自动重连。
  void OnFrontDisconnected(int nReason) override;

  /// 心跳超时警告。
  void OnHeartBeatWarning(int nTimeLapse) override;

  /// 登录请求响应
  void OnRspUserLogin(CThostFtdcRspUserLoginField *pRspUserLogin, CThostFtdcRspInfoField *pRspInfo, int nRequestID,
                      bool bIsLast) override;

  /// 登出请求响应
  void OnRspUserLogout(CThostFtdcUserLogoutField *pUserLogout, CThostFtdcRspInfoField *pRspInfo, int nRequestID,
                       bool bIsLast) override;

  /// 错误应答
  void OnRspError(CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) override;

  /// 订阅行情应答
  void OnRspSubMarketData(CThostFtdcSpecificInstrumentField *pSpecificInstrument, CThostFtdcRspInfoField *pRspInfo,
                          int nRequestID, bool bIsLast) override;

  /// 取消订阅行情应答
  void OnRspUnSubMarketData(CThostFtdcSpecificInstrumentField *pSpecificInstrument, CThostFtdcRspInfoField *pRspInfo,
                            int nRequestID, bool bIsLast) override;

  /// 深度行情通知
  void OnRtnDepthMarketData(CThostFtdcDepthMarketDataField *pDepthMarketData) override;

private:
  CThostFtdcMdApi *api_ = nullptr;
  MarketDataCtp *service_ = nullptr;
  CtpConfig config_;
  int request_id_ = 0;
};

} // namespace kungfu::wingchun::ctp
