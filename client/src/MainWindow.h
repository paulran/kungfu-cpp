// SPDX-License-Identifier: Apache-2.0

#ifndef KF_QT_CLIENT_MAIN_WINDOW_H
#define KF_QT_CLIENT_MAIN_WINDOW_H

#include <QJsonValue>
#include <QMainWindow>

class QLineEdit;
class QPushButton;
class QLabel;
class QTimer;

namespace kfclient {

class ApiClient;
class StrategyTab;
class OrderBook;
class OrderEntry;
class LeftBottomTabs;

// 4 象限交易终端主窗口：
//   左上 策略列表 / 左下 资金·持仓·当前委托·订单历史·历史成交·日志
//   右上 行情 5 档 BOOK / 右下 手动下单（买入/卖出）
class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget *parent = nullptr);

private slots:
  void onConnect();
  void onDisconnect();
  void onClientConnected();
  void onClientDisconnected();
  void onLog(const QString &msg);
  void onApiResponse(quint64 requestId, const QJsonValue &data, const QString &error);
  void onRefreshTimeout();

private:
  void appendLog(const QString &msg);
  void setStatusConnected(bool connected);

  ApiClient *client_;
  QLineEdit *hostEdit_;
  QLineEdit *portEdit_;
  QPushButton *connectBtn_;
  QPushButton *disconnectBtn_;
  QLabel *statusLabel_;

  StrategyTab *strategyTab_;
  OrderBook *orderBook_;
  OrderEntry *orderEntry_;
  LeftBottomTabs *leftBottom_;
  QTimer *refreshTimer_;

  quint64 pendingLoc_ = 0;  // request id of last get_locations
  quint64 pendingDay_ = 0;  // request id of last get_trading_day
};

}  // namespace kfclient

#endif  // KF_QT_CLIENT_MAIN_WINDOW_H
