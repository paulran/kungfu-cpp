// SPDX-License-Identifier: Apache-2.0

#include "MainWindow.h"

#include "ApiClient.h"
#include "LeftBottomTabs.h"
#include "OrderBook.h"
#include "OrderEntry.h"
#include "StrategyTab.h"

#include <QDateTime>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace kfclient {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), client_(new ApiClient(this)) {
  setWindowTitle(tr("Kungfu-cpp 交易终端"));
  resize(1400, 860);

  auto *central = new QWidget(this);
  setCentralWidget(central);
  auto *root = new QVBoxLayout(central);
  root->setContentsMargins(6, 6, 6, 6);
  root->setSpacing(6);

  // ---- 顶部连接条 ----
  auto *bar = new QHBoxLayout();
  bar->addWidget(new QLabel(tr("Host:"), this));
  hostEdit_ = new QLineEdit(QStringLiteral("127.0.0.1"), this);
  hostEdit_->setMaximumWidth(140);
  bar->addWidget(hostEdit_);
  bar->addWidget(new QLabel(tr("Port:"), this));
  portEdit_ = new QLineEdit(QStringLiteral("7788"), this);
  portEdit_->setMaximumWidth(70);
  bar->addWidget(portEdit_);
  connectBtn_ = new QPushButton(tr("连接"), this);
  connectBtn_->setObjectName(QStringLiteral("primaryBtn"));
  disconnectBtn_ = new QPushButton(tr("断开"), this);
  disconnectBtn_->setEnabled(false);
  bar->addWidget(connectBtn_);
  bar->addWidget(disconnectBtn_);
  bar->addStretch();
  statusLabel_ = new QLabel(tr("状态: 未连接"), this);
  statusLabel_->setObjectName(QStringLiteral("statusLabel"));
  statusLabel_->setProperty("connected", false);
  bar->addWidget(statusLabel_);
  root->addLayout(bar);

  // ---- 4 象限组件 ----
  strategyTab_ = new StrategyTab(client_, this);
  orderBook_ = new OrderBook(client_, this);
  orderEntry_ = new OrderEntry(client_, this);
  leftBottom_ = new LeftBottomTabs(client_, this);

  auto *strategyBox = new QGroupBox(tr("策略"), this);
  auto *sl = new QVBoxLayout(strategyBox);
  sl->setContentsMargins(0, 0, 0, 0);
  sl->setSpacing(0);
  sl->addWidget(strategyTab_);

  auto *mainSplit = new QSplitter(Qt::Horizontal, this);
  mainSplit->setChildrenCollapsible(false);

  // 左列：策略(上) + 左下6标签(下)
  auto *leftSplit = new QSplitter(Qt::Vertical, mainSplit);
  leftSplit->setChildrenCollapsible(false);
  leftSplit->addWidget(strategyBox);
  leftSplit->addWidget(leftBottom_);
  leftSplit->setStretchFactor(0, 1);
  leftSplit->setStretchFactor(1, 2);
  leftSplit->setSizes({220, 480});

  // 右列：行情BOOK(上) + 手动下单(下)
  auto *rightSplit = new QSplitter(Qt::Vertical, mainSplit);
  rightSplit->setChildrenCollapsible(false);
  rightSplit->addWidget(orderBook_);
  rightSplit->addWidget(orderEntry_);
  rightSplit->setStretchFactor(0, 1);
  rightSplit->setStretchFactor(1, 1);
  rightSplit->setSizes({420, 280});

  mainSplit->addWidget(leftSplit);
  mainSplit->addWidget(rightSplit);
  // 左列占 2/3（策略+数据表），右列占 1/3（行情+下单）
  mainSplit->setStretchFactor(0, 2);
  mainSplit->setStretchFactor(1, 1);
  mainSplit->setSizes({880, 440});
  root->addWidget(mainSplit, 1);

  // ---- 接线 ----
  connect(connectBtn_, &QPushButton::clicked, this, &MainWindow::onConnect);
  connect(disconnectBtn_, &QPushButton::clicked, this, &MainWindow::onDisconnect);

  connect(client_, &ApiClient::connected, this, &MainWindow::onClientConnected);
  connect(client_, &ApiClient::disconnected, this, &MainWindow::onClientDisconnected);
  connect(client_, &ApiClient::responseReceived, this, &MainWindow::onApiResponse);
  connect(client_, &ApiClient::socketError, this, [this](const QString &m) { onLog(m); });
  connect(client_, &ApiClient::logMessage, this, &MainWindow::onLog);

  // 各业务组件（StrategyTab / OrderBook / OrderEntry / LeftBottomTabs）在各自构造函数里
  // 自行 connect 到 ApiClient 的 responseReceived / quoteReceived / orderReceived …
  // 信号（同类访问私有/公有成员合法，与原 tab 模式一致），按自身 pending_ 过滤响应。
  // MainWindow 这里只接自己关心的信号（连接状态 / 全局响应分发 / 日志）。

  connect(client_, &ApiClient::brokerStateReceived, this,
          [this](const BrokerStateInfo &s) {
            onLog(tr("BrokerState uid=%1 -> %2")
                      .arg(s.locationUid, 8, 16, QChar('0'))
                      .arg(s.state));
          });
  connect(client_, &ApiClient::genericBinaryReceived, this,
          [this](const QString &n, const QString &s) { onLog(tr("[%1] %2").arg(n, s)); });

  // 日志回调（新组件用 std::function 转发；StrategyTab 用 signal）。
  connect(strategyTab_, &StrategyTab::logMessage, this, &MainWindow::onLog);
  orderBook_->setLogHandler([this](const QString &s) { onLog(s); });
  orderEntry_->setLogHandler([this](const QString &s) { onLog(s); });
  leftBottom_->setLogHandler([this](const QString &s) { onLog(s); });

  // 周期刷新（保持策略 live 状态与 TD/MD combo 新鲜）。
  refreshTimer_ = new QTimer(this);
  refreshTimer_->setInterval(5000);
  connect(refreshTimer_, &QTimer::timeout, this, &MainWindow::onRefreshTimeout);

  appendLog(tr("就绪。连接 kf_api（默认 127.0.0.1:7788）。"));
}

void MainWindow::onConnect() {
  bool ok = false;
  quint16 port = portEdit_->text().toUShort(&ok);
  if (!ok) {
    appendLog(tr("端口无效"));
    return;
  }
  client_->connectToHost(hostEdit_->text(), port);
}

void MainWindow::onDisconnect() {
  client_->disconnectFromHost();
}

void MainWindow::onClientConnected() {
  setStatusConnected(true);
  connectBtn_->setEnabled(false);
  disconnectBtn_->setEnabled(true);
  pendingLoc_ = client_->getLocations();
  pendingDay_ = client_->getTradingDay();
  strategyTab_->onRefresh();
  refreshTimer_->start();
}

void MainWindow::onClientDisconnected() {
  setStatusConnected(false);
  connectBtn_->setEnabled(true);
  disconnectBtn_->setEnabled(false);
  refreshTimer_->stop();
}

void MainWindow::onLog(const QString &msg) { appendLog(msg); }

void MainWindow::appendLog(const QString &msg) {
  QString line = QStringLiteral("[%1] %2").arg(
      QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")), msg);
  leftBottom_->appendLog(line);
}

void MainWindow::setStatusConnected(bool connected) {
  statusLabel_->setText(connected ? tr("状态: 已连接") : tr("状态: 未连接"));
  statusLabel_->setProperty("connected", connected);
  statusLabel_->style()->unpolish(statusLabel_);
  statusLabel_->style()->polish(statusLabel_);
}

void MainWindow::onApiResponse(quint64 requestId, const QJsonValue &data, const QString &error) {
  if (requestId == pendingLoc_) {
    pendingLoc_ = 0;
    if (!error.isEmpty()) {
      appendLog(tr("get_locations 错误: %1").arg(error));
      return;
    }
    QJsonArray arr = data.toArray();
    strategyTab_->setLocations(arr);
    orderBook_->setLocations(arr);
    orderEntry_->setLocations(arr);
    appendLog(tr("加载 %1 个 location").arg(arr.size()));
    return;
  }
  if (requestId == pendingDay_) {
    pendingDay_ = 0;
    if (!error.isEmpty()) {
      appendLog(tr("get_trading_day 错误: %1").arg(error));
      return;
    }
    QString day = data.isString()
                      ? data.toString()
                      : QString::fromUtf8(QJsonDocument(data.toObject()).toJson(QJsonDocument::Compact));
    appendLog(tr("交易日: %1").arg(day));
    return;
  }
  // 其余响应（issue_order / request_market_data / cancel_order / request_position …）
  // 由各组件按自身 pending_ 处理。
}

void MainWindow::onRefreshTimeout() {
  if (!client_->isConnected()) return;
  if (pendingLoc_ == 0) pendingLoc_ = client_->getLocations();
  if (pendingDay_ == 0) pendingDay_ = client_->getTradingDay();
}

}  // namespace kfclient
