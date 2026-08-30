// SPDX-License-Identifier: Apache-2.0
//
// 右上：行情 5 档盘口 BOOK（卖盘上、买盘下）。普通 QWidget，无 Q_OBJECT。
// 由 MainWindow 用函数指针 connect 接 ApiClient::quoteReceived / responseReceived。

#ifndef KF_QT_CLIENT_ORDER_BOOK_H
#define KF_QT_CLIENT_ORDER_BOOK_H

#include "ApiClient.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QString>
#include <QWidget>
#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace kfclient {

class ApiClient;

class OrderBook : public QWidget {
public:
  explicit OrderBook(ApiClient *client, QWidget *parent = nullptr);

  void setLocations(const QJsonArray &locations);
  void setLogHandler(const std::function<void(const QString &)> &h) { log_ = h; }

  // 由 MainWindow 用函数指针 connect 接 ApiClient 信号。
  void onQuoteReceived(const QuoteInfo &quote);
  void onResponse(quint64 requestId, const QJsonValue &data, const QString &error);

private:
  void onSubscribe();
  void onCancel();
  void log(const QString &msg) { if (log_) log_(msg); }
  void clearBook();

  std::function<void(const QString &)> log_;

  ApiClient *client_;
  QComboBox *mdCombo_;
  QLineEdit *exchangeEdit_;
  QLineEdit *instrumentEdit_;
  QPushButton *subBtn_;
  QPushButton *cancelBtn_;
  QLabel *title_;
  QLabel *lastLabel_;
  QTableWidget *askBook_;  // 5 行：卖5..卖1
  QTableWidget *bidBook_;  // 5 行：买1..买5
  QString displayKey_;     // "instrument@exchange" 当前展示合约
  QJsonArray locations_;
  QHash<quint64, QString> pending_;
};

}  // namespace kfclient

#endif  // KF_QT_CLIENT_ORDER_BOOK_H
