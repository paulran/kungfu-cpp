#pragma once

// CTP trade front SPI. Like the MD SPI this runs on the CTP API worker
// thread and only forwards converted data to the owning TraderCtp service,
// which runs the actual handling on the kungfu event loop thread.

#include <ThostFtdcTraderApi.h>

#include "ctp_common.h"

namespace kungfu::wingchun::ctp {

class TraderCtp;

class CtpTraderSpi : public CThostFtdcTraderSpi {
public:
  CtpTraderSpi(CThostFtdcTraderApi *api, TraderCtp *service, CtpConfig config);

  void OnFrontConnected() override;

  void OnFrontDisconnected(int nReason) override;

  void OnHeartBeatWarning(int nTimeLapse) override;

  void OnRspAuthenticate(CThostFtdcRspAuthenticateField *pRspAuthenticateField, CThostFtdcRspInfoField *pRspInfo,
                         int nRequestID, bool bIsLast) override;

  void OnRspUserLogin(CThostFtdcRspUserLoginField *pRspUserLogin, CThostFtdcRspInfoField *pRspInfo, int nRequestID,
                      bool bIsLast) override;

  void OnRspUserLogout(CThostFtdcUserLogoutField *pUserLogout, CThostFtdcRspInfoField *pRspInfo, int nRequestID,
                       bool bIsLast) override;

  void OnRspError(CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) override;

  /// 报单录入请求响应（柜台校验失败，如资金不足）
  void OnRspOrderInsert(CThostFtdcInputOrderField *pInputOrder, CThostFtdcRspInfoField *pRspInfo, int nRequestID,
                        bool bIsLast) override;

  /// 报单操作请求响应（撤单校验失败）
  void OnRspOrderAction(CThostFtdcInputOrderActionField *pInputOrderAction, CThostFtdcRspInfoField *pRspInfo,
                        int nRequestID, bool bIsLast) override;

  /// 投资者结算结果确认响应（ReqSettlementInfoConfirm 的响应）
  void OnRspSettlementInfoConfirm(CThostFtdcSettlementInfoConfirmField *pSettlementInfoConfirm,
                                  CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) override;

  /// 请求查询投资者结算结果确认响应（ReqQrySettlementInfoConfirm 的响应，
  /// 用于判断当日是否已确认结算单，避免重复确认）
  void OnRspQrySettlementInfoConfirm(CThostFtdcSettlementInfoConfirmField *pSettlementInfoConfirm,
                                     CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) override;

  /// 请求查询投资者结算结果响应（ReqQrySettlementInfo 的响应，结算单正文）
  void OnRspQrySettlementInfo(CThostFtdcSettlementInfoField *pSettlementInfo, CThostFtdcRspInfoField *pRspInfo,
                              int nRequestID, bool bIsLast) override;

  void OnRspQryInstrument(CThostFtdcInstrumentField *pInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID,
                          bool bIsLast) override;

  void OnRspQryTradingAccount(CThostFtdcTradingAccountField *pTradingAccount, CThostFtdcRspInfoField *pRspInfo,
                              int nRequestID, bool bIsLast) override;

  void OnRspQryInvestorPosition(CThostFtdcInvestorPositionField *pInvestorPosition, CThostFtdcRspInfoField *pRspInfo,
                                int nRequestID, bool bIsLast) override;

  void OnRspQryOrder(CThostFtdcOrderField *pOrder, CThostFtdcRspInfoField *pRspInfo, int nRequestID,
                     bool bIsLast) override;

  void OnRspQryTrade(CThostFtdcTradeField *pTrade, CThostFtdcRspInfoField *pRspInfo, int nRequestID,
                     bool bIsLast) override;

  void OnRtnOrder(CThostFtdcOrderField *pOrder) override;

  void OnRtnTrade(CThostFtdcTradeField *pTrade) override;

  /// 报单录入错误回报（交易所拒单）
  void OnErrRtnOrderInsert(CThostFtdcInputOrderField *pInputOrder, CThostFtdcRspInfoField *pRspInfo) override;

  /// 报单操作错误回报
  void OnErrRtnOrderAction(CThostFtdcOrderActionField *pOrderAction, CThostFtdcRspInfoField *pRspInfo) override;

private:
  CThostFtdcTraderApi *api_ = nullptr;
  TraderCtp *service_ = nullptr;
  CtpConfig config_;
  int request_id_ = 0;
};

} // namespace kungfu::wingchun::ctp
