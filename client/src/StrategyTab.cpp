// SPDX-License-Identifier: Apache-2.0

#include "StrategyTab.h"

#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace kfclient {

StrategyTab::StrategyTab(ApiClient *client, QWidget *parent)
    : QWidget(parent), client_(client), table_(new QTableWidget(this)) {
  auto *layout = new QVBoxLayout(this);

  table_->setColumnCount(ColStCount);
  table_->setHorizontalHeaderLabels(
      {tr("UID"), tr("Category"), tr("Group"), tr("Name"), tr("Mode"), tr("Uname"), tr("Live")});
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  layout->addWidget(table_);

  auto *btnRow = new QHBoxLayout();
  auto *refreshBtn = new QPushButton(tr("Refresh Strategies"), this);
  auto *startBtn = new QPushButton(tr("Start Strategy..."), this);
  auto *stopBtn = new QPushButton(tr("Stop Strategy"), this);
  auto *subBtn = new QPushButton(tr("Subscribe"), this);
  auto *unsubBtn = new QPushButton(tr("Unsubscribe"), this);
  btnRow->addWidget(refreshBtn);
  btnRow->addWidget(startBtn);
  btnRow->addWidget(stopBtn);
  btnRow->addStretch();
  btnRow->addWidget(subBtn);
  btnRow->addWidget(unsubBtn);
  layout->addLayout(btnRow);

  connect(refreshBtn, &QPushButton::clicked, this, &StrategyTab::onRefresh);
  connect(startBtn, &QPushButton::clicked, this, &StrategyTab::onStart);
  connect(stopBtn, &QPushButton::clicked, this, &StrategyTab::onStop);
  connect(subBtn, &QPushButton::clicked, this, &StrategyTab::onSubscribe);
  connect(unsubBtn, &QPushButton::clicked, this, &StrategyTab::onUnsubscribe);

  connect(client_, &ApiClient::responseReceived, this, &StrategyTab::onResponse);

  // Auto-refresh once the client connects.
  connect(client_, &ApiClient::connected, this, &StrategyTab::onRefresh);
}

void StrategyTab::setStrategies(const QJsonArray &strategies) {
  strategies_ = strategies;
  table_->setRowCount(0);
  table_->setRowCount(static_cast<int>(strategies.size()));
  for (int i = 0; i < strategies.size(); ++i) {
    QJsonObject o = strategies.at(i).toObject();
    table_->setItem(i, ColStUid, new QTableWidgetItem(o.value("uid").toString()));
    table_->setItem(i, ColStCategory, new QTableWidgetItem(o.value("category").toString()));
    table_->setItem(i, ColStGroup, new QTableWidgetItem(o.value("group").toString()));
    table_->setItem(i, ColStName, new QTableWidgetItem(o.value("name").toString()));
    table_->setItem(i, ColStMode, new QTableWidgetItem(o.value("mode").toString()));
    table_->setItem(i, ColStUname, new QTableWidgetItem(o.value("uname").toString()));
    bool live = o.value("live").toBool();
    auto *liveItem = new QTableWidgetItem(live ? tr("yes") : tr("no"));
    liveItem->setForeground(live ? QColor(Qt::darkGreen) : QColor(Qt::gray));
    table_->setItem(i, ColStLive, liveItem);
  }
  table_->resizeColumnsToContents();
}

void StrategyTab::setLocations(const QJsonArray &locations) { locations_ = locations; }

void StrategyTab::onRefresh() {
  if (!client_->isConnected()) {
    appendLog(tr("not connected"));
    return;
  }
  quint64 rid = client_->getStrategies();
  pending_[rid] = QStringLiteral("get_strategies");
}

QJsonObject StrategyTab::currentStrategyLocation(int row) const {
  if (row < 0 || row >= table_->rowCount()) return {};
  // Resolve by uid first (preferred), fall back to category/group/name/mode.
  QString uid = table_->item(row, ColStUid)->text();
  QString category = table_->item(row, ColStCategory)->text();
  QString group = table_->item(row, ColStGroup)->text();
  QString name = table_->item(row, ColStName)->text();
  QString mode = table_->item(row, ColStMode)->text();
  QJsonObject loc;
  loc["uid"] = uid;
  loc["category"] = category;
  loc["group"] = group;
  loc["name"] = name;
  loc["mode"] = mode;
  return loc;
}

void StrategyTab::onStart() {
  QDialog dlg(this);
  dlg.setWindowTitle(tr("Start Strategy"));
  auto *form = new QFormLayout(&dlg);

  auto *modeEdit = new QLineEdit(QStringLiteral("live"), &dlg);
  auto *groupEdit = new QLineEdit(QStringLiteral("sim"), &dlg);
  auto *nameEdit = new QLineEdit(QStringLiteral("sim"), &dlg);
  auto *strategyEdit = new QLineEdit(QStringLiteral("kungfu_strategy_101"), &dlg);
  auto *exeEdit = new QLineEdit(QStringLiteral("./Release/kf_strategy"), &dlg);
  auto *argsEdit = new QLineEdit(&dlg);

  form->addRow(tr("mode:"), modeEdit);
  form->addRow(tr("group:"), groupEdit);
  form->addRow(tr("name:"), nameEdit);
  form->addRow(tr("strategy:"), strategyEdit);
  form->addRow(tr("exe path:"), exeEdit);
  form->addRow(tr("args (optional):"), argsEdit);

  auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  form->addRow(btns);
  connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted) return;

  QJsonObject data;
  data["mode"] = modeEdit->text();
  data["group"] = groupEdit->text();
  data["name"] = nameEdit->text();
  data["strategy"] = strategyEdit->text();
  data["exe"] = exeEdit->text();
  data["args"] = argsEdit->text();
  quint64 rid = client_->startStrategy(data);
  pending_[rid] = QStringLiteral("start_strategy");
  appendLog(tr("sent start_strategy (id=%1)").arg(rid));
}

void StrategyTab::onStop() {
  int row = table_->currentRow();
  if (row < 0) {
    QMessageBox::information(this, tr("Stop Strategy"), tr("Select a strategy row first."));
    return;
  }
  QJsonObject loc = currentStrategyLocation(row);
  if (loc.isEmpty()) return;
  if (QMessageBox::question(this, tr("Stop Strategy"),
                            tr("Stop strategy %1/%2?").arg(loc.value("group").toString(), loc.value("name").toString())) !=
      QMessageBox::Yes)
    return;
  quint64 rid = client_->stopStrategy(loc);
  pending_[rid] = QStringLiteral("stop_strategy");
  appendLog(tr("sent stop_strategy (id=%1)").arg(rid));
}

void StrategyTab::onSubscribe() {
  int row = table_->currentRow();
  if (row < 0) {
    QMessageBox::information(this, tr("Subscribe"), tr("Select a strategy row first."));
    return;
  }
  QJsonObject loc = currentStrategyLocation(row);
  quint64 rid = client_->subscribeStrategy(loc);
  pending_[rid] = QStringLiteral("subscribe_strategy");
  appendLog(tr("sent subscribe_strategy (id=%1)").arg(rid));
}

void StrategyTab::onUnsubscribe() {
  int row = table_->currentRow();
  if (row < 0) {
    QMessageBox::information(this, tr("Unsubscribe"), tr("Select a strategy row first."));
    return;
  }
  QJsonObject loc = currentStrategyLocation(row);
  quint64 rid = client_->unsubscribeStrategy(loc);
  pending_[rid] = QStringLiteral("unsubscribe_strategy");
  appendLog(tr("sent unsubscribe_strategy (id=%1)").arg(rid));
}

void StrategyTab::onResponse(quint64 requestId, const QJsonValue &data, const QString &error) {
  auto it = pending_.find(requestId);
  if (it == pending_.end()) return;
  QString method = it.value();
  pending_.erase(it);

  if (!error.isEmpty()) {
    appendLog(tr("%1 ERROR: %2").arg(method, error));
    QMessageBox::warning(this, method, tr("Request failed: %1").arg(error));
    return;
  }

  if (method == QStringLiteral("get_strategies")) {
    QJsonArray arr = data.toArray();
    setStrategies(arr);
    appendLog(tr("loaded %1 strategies").arg(arr.size()));
  } else if (method == QStringLiteral("start_strategy")) {
    QString pid = data.toObject().value("pid").toVariant().toString();
    appendLog(tr("strategy started, pid=%1").arg(pid));
    onRefresh(); // refresh the live table
  } else if (method == QStringLiteral("stop_strategy")) {
    appendLog(tr("stop_strategy accepted, strategy will exit asynchronously"));
    QTimer::singleShot(1200, this, [this] { onRefresh(); });
  } else if (method == QStringLiteral("subscribe_strategy")) {
    appendLog(tr("subscribed to strategy %1").arg(data.toObject().value("strategy_uid").toString()));
  } else if (method == QStringLiteral("unsubscribe_strategy")) {
    appendLog(tr("unsubscribed from strategy"));
  }
}

} // namespace kfclient
