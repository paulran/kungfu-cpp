// SPDX-License-Identifier: Apache-2.0
//
// 左下：资金 / 持仓 / 当前委托 / 订单历史 / 历史成交 / 日志 六个标签页。
// 普通 QWidget，无 Q_OBJECT。
//
// 当前委托页自带 TD 账户下拉框：撤单所需的 TD 由用户在该页选择（与原 TradingTab
// 行为一致），order_id 取自 Order 二进制推送（精确 quint64，无 JSON 精度问题），
// 直接调 client_->cancelOrder(td, oid)。不依赖 orderId->TD 映射。

#ifndef KF_QT_CLIENT_LEFT_BOTTOM_TABS_H
#define KF_QT_CLIENT_LEFT_BOTTOM_TABS_H

#include "ApiClient.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QString>
#include <QWidget>
#include <functional>

class QComboBox;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTabWidget;

namespace kfclient {

class ApiClient;

class LeftBottomTabs : public QWidget {
public:
  explicit LeftBottomTabs(ApiClient *client, QWidget *parent = nullptr);

  void setLocations(const QJsonArray &locations);
  void setLogHandler(const std::function<void(const QString &)> &h) { log_ = h; }

  void onOrderReceived(const OrderInfo &order);
  void onTradeReceived(const TradeInfo &trade);
  void onPositionReceived(const PositionInfo &p);
  void onAssetReceived(const AssetInfo &a);
  void onResponse(quint64 requestId, const QJsonValue &data, const QString &error);

  // MainWindow 的 appendLog 转发到这里，写入日志页。
  void appendLog(const QString &msg);

private:
  void onRequestPosition();
  void onCancelOrder();
  bool isActive(const OrderInfo &o) const;
  void log(const QString &msg) { if (log_) log_(msg); }

  std::function<void(const QString &)> log_;

  enum PosCol {
    ColPInstrument, ColPExchange, ColPDirection, ColPVolume, ColPYesterday, ColPFrozen,
    ColPAvgOpen, ColPCost, ColPLast, ColPUnrealizedPnl, ColPRealizedPnl, ColPHolder, ColPCount
  };
  enum AssetCol {
    ColAHolder, ColACategory, ColADay, ColAInitEquity, ColAStaticEquity, ColADynamicEquity,
    ColAAvail, ColAMarketValue, ColAMargin, ColAFrozenCash, ColAUnrealizedPnl, ColARealizedPnl,
    ColACount
  };
  enum OrderCol {
    ColOOrderId, ColOInstrument, ColOExchange, ColOSide, ColOOffset, ColOPrice, ColOVolume,
    ColOVolumeLeft, ColOStatus, ColOErrorMsg, ColOCount
  };
  enum TradeCol {
    ColTTradeId, ColTOrderId, ColTInstrument, ColTExchange, ColTSide, ColTOffset, ColTPrice,
    ColTVolume, ColTCount
  };
  // 当前委托（active）列
  enum ActiveCol {
    ColCOrderId, ColCInstrument, ColCExchange, ColCSide, ColCOffset, ColCPrice, ColCVolume,
    ColCVolumeLeft, ColCStatus, ColCCount
  };

  ApiClient *client_;
  QTabWidget *tabs_;
  QTableWidget *assetTable_;
  QTableWidget *posTable_;
  QTableWidget *activeTable_;
  QTableWidget *orderTable_;
  QTableWidget *tradeTable_;
  QPlainTextEdit *logView_;
  QComboBox *activeTdCombo_;  // 当前委托页的 TD 账户选择（撤单用）
  QPushButton *reqPosBtn_;
  QPushButton *cancelBtn_;

  QHash<quint64, int> activeRowMap_;  // orderId -> row in activeTable_
  QHash<quint64, int> orderRowMap_;   // orderId -> row in orderTable_
  QHash<QString, int> posRowMap_;     // "holder|ex|instr|dir" -> row
  QHash<quint32, int> assetRowMap_;   // holderUid -> row
  QHash<quint64, QString> pending_;   // rid -> method（request_position / cancel_order）
};

}  // namespace kfclient

#endif  // KF_QT_CLIENT_LEFT_BOTTOM_TABS_H
