// SPDX-License-Identifier: Apache-2.0
//
// 右下：手动下单，买入 / 卖出 两页。普通 QWidget，无 Q_OBJECT。
// side 由所在页决定（买入=Buy，卖出=Sell），不设 side 选择框。

#ifndef KF_QT_CLIENT_ORDER_ENTRY_H
#define KF_QT_CLIENT_ORDER_ENTRY_H

#include "ApiClient.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QString>
#include <QWidget>
#include <functional>

class QComboBox;
class QLineEdit;
class QPushButton;
class QTabWidget;

namespace kfclient {

class ApiClient;

class OrderEntry : public QWidget {
public:
  explicit OrderEntry(ApiClient *client, QWidget *parent = nullptr);

  void setLocations(const QJsonArray &locations);
  void setLogHandler(const std::function<void(const QString &)> &h) { log_ = h; }

  void onResponse(quint64 requestId, const QJsonValue &data, const QString &error);

private:
  struct Page {
    QWidget *page;
    QComboBox *tdCombo;
    QLineEdit *exchange;
    QLineEdit *instrument;
    QLineEdit *price;
    QLineEdit *volume;
    QComboBox *offset;
    QComboBox *priceType;
    QPushButton *btn;
  };

  Page buildPage(const QString &btnText, const QString &objectName, const QString &side);
  void onIssue(const Page &p, const QString &side);
  void log(const QString &msg) { if (log_) log_(msg); }

  std::function<void(const QString &)> log_;

  ApiClient *client_;
  QTabWidget *tabs_;
  Page buy_;
  Page sell_;
  QJsonArray locations_;
  QHash<quint64, QString> pending_;  // rid -> "issue_order"
};

}  // namespace kfclient

#endif  // KF_QT_CLIENT_ORDER_ENTRY_H
