// SPDX-License-Identifier: Apache-2.0

#ifndef KF_QT_CLIENT_STRATEGY_TAB_H
#define KF_QT_CLIENT_STRATEGY_TAB_H

#include "ApiClient.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QString>
#include <QWidget>

class QTableWidget;
class QPushButton;

namespace kfclient {

class ApiClient;

// Strategy management page: list strategies, start/stop, subscribe/unsubscribe.
class StrategyTab : public QWidget {
  Q_OBJECT
public:
  explicit StrategyTab(ApiClient *client, QWidget *parent = nullptr);

  // Refresh the strategy table from the get_strategies response.
  void setStrategies(const QJsonArray &strategies);
  // Refresh the locations table from get_locations (used to populate MD/TD pickers).
  void setLocations(const QJsonArray &locations);

  // Re-query the strategy list from the server (public so the main window can
  // trigger a refresh right after connecting without relying on moc string lookup).
  void onRefresh();

signals:
  void logMessage(const QString &msg);

private slots:
  void onStart();
  void onStop();
  void onSubscribe();
  void onUnsubscribe();
  void onResponse(quint64 requestId, const QJsonValue &data, const QString &error);

private:
  // Columns of the strategy table.
  enum StrategyCol { ColStUid, ColStCategory, ColStGroup, ColStName, ColStMode, ColStUname, ColStLive, ColStCount };

  QJsonObject currentStrategyLocation(int row) const;
  void appendLog(const QString &msg) { emit logMessage(msg); }

  ApiClient *client_;
  QTableWidget *table_;
  QHash<quint64, QString> pending_; // request_id -> tag (for log context)

  // Cached raw locations/strategies for pickers elsewhere.
  QJsonArray locations_;
  QJsonArray strategies_;
};

} // namespace kfclient

#endif // KF_QT_CLIENT_STRATEGY_TAB_H
