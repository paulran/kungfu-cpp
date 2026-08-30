// SPDX-License-Identifier: Apache-2.0

#include "LeftBottomTabs.h"

#include <QColor>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace kfclient {

LeftBottomTabs::LeftBottomTabs(ApiClient *client, QWidget *parent)
    : QWidget(parent), client_(client),
      tabs_(new QTabWidget(this)),
      assetTable_(new QTableWidget(this)),
      posTable_(new QTableWidget(this)),
      activeTable_(new QTableWidget(this)),
      orderTable_(new QTableWidget(this)),
      tradeTable_(new QTableWidget(this)),
      logView_(new QPlainTextEdit(this)),
      activeTdCombo_(new QComboBox(this)) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(tabs_);

  auto setup = [](QTableWidget *t, const QStringList &headers) {
    t->setColumnCount(headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->horizontalHeader()->setStretchLastSection(true);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setAlternatingRowColors(true);
    t->verticalHeader()->setVisible(false);
  };
  setup(assetTable_, {tr("账户"), tr("类别"), tr("交易日"), tr("期初权益"), tr("静态权益"),
                      tr("动态权益"), tr("可用"), tr("市值"), tr("保证金"), tr("冻结资金"),
                      tr("浮动盈亏"), tr("平仓盈亏")});
  setup(posTable_, {tr("合约"), tr("交易所"), tr("方向"), tr("持仓"), tr("昨仓"), tr("冻结"),
                    tr("开仓均价"), tr("成本"), tr("最新"), tr("浮动盈亏"), tr("平仓盈亏"),
                    tr("账户")});
  setup(activeTable_, {tr("订单号"), tr("合约"), tr("交易所"), tr("方向"), tr("开平"), tr("价格"),
                       tr("数量"), tr("剩余"), tr("状态")});
  setup(orderTable_, {tr("订单号"), tr("合约"), tr("交易所"), tr("方向"), tr("开平"), tr("价格"),
                      tr("数量"), tr("剩余"), tr("状态"), tr("错误")});
  setup(tradeTable_, {tr("成交号"), tr("订单号"), tr("合约"), tr("交易所"), tr("方向"), tr("开平"),
                       tr("价格"), tr("数量")});

  logView_->setReadOnly(true);
  logView_->setMaximumBlockCount(5000);
  logView_->setObjectName(QStringLiteral("logView"));

  // 资金页
  tabs_->addTab(assetTable_, tr("资金"));

  // 持仓页（含 请求持仓/资金 按钮）
  auto *posPage = new QWidget(this);
  auto *posLay = new QVBoxLayout(posPage);
  posLay->setContentsMargins(0, 0, 0, 0);
  auto *posBtnRow = new QHBoxLayout();
  reqPosBtn_ = new QPushButton(tr("请求持仓/资金"), posPage);
  posBtnRow->addWidget(reqPosBtn_);
  posBtnRow->addStretch();
  posLay->addLayout(posBtnRow);
  posLay->addWidget(posTable_, 1);
  tabs_->addTab(posPage, tr("持仓"));

  // 当前委托页（TD 账户下拉 + 取消委托 按钮）
  auto *activePage = new QWidget(this);
  auto *actLay = new QVBoxLayout(activePage);
  actLay->setContentsMargins(0, 0, 0, 0);
  auto *actBtnRow = new QHBoxLayout();
  actBtnRow->addWidget(new QLabel(tr("撤单 TD:"), activePage));
  activeTdCombo_->setMinimumWidth(170);
  actBtnRow->addWidget(activeTdCombo_);
  actBtnRow->addStretch();
  cancelBtn_ = new QPushButton(tr("取消委托"), activePage);
  actBtnRow->addWidget(cancelBtn_);
  actLay->addLayout(actBtnRow);
  actLay->addWidget(activeTable_, 1);
  tabs_->addTab(activePage, tr("当前委托"));

  tabs_->addTab(orderTable_, tr("订单历史"));
  tabs_->addTab(tradeTable_, tr("历史成交"));
  tabs_->addTab(logView_, tr("日志"));

  connect(reqPosBtn_, &QPushButton::clicked, this, &LeftBottomTabs::onRequestPosition);
  connect(cancelBtn_, &QPushButton::clicked, this, &LeftBottomTabs::onCancelOrder);

  // 自行连接 ApiClient 推送/响应信号（与原 tab 模式一致）。
  connect(client_, &ApiClient::responseReceived, this, &LeftBottomTabs::onResponse);
  connect(client_, &ApiClient::orderReceived, this, &LeftBottomTabs::onOrderReceived);
  connect(client_, &ApiClient::tradeReceived, this, &LeftBottomTabs::onTradeReceived);
  connect(client_, &ApiClient::positionReceived, this, &LeftBottomTabs::onPositionReceived);
  connect(client_, &ApiClient::assetReceived, this, &LeftBottomTabs::onAssetReceived);
}

void LeftBottomTabs::setLocations(const QJsonArray &locations) {
  activeTdCombo_->clear();
  for (const auto &v : locations) {
    QJsonObject o = v.toObject();
    if (o.value("category").toString() == QStringLiteral("td")) {
      QString label = QStringLiteral("%1/%2/%3 [%4]")
                          .arg(o.value("mode").toString(), o.value("group").toString(),
                               o.value("name").toString(), o.value("uid").toString());
      activeTdCombo_->addItem(label, o);
    }
  }
}

bool LeftBottomTabs::isActive(const OrderInfo &o) const {
  if (o.status == QStringLiteral("Filled") || o.status == QStringLiteral("Cancelled") ||
      o.status == QStringLiteral("Error"))
    return false;
  return o.volumeLeft > 0;
}

void LeftBottomTabs::onRequestPosition() {
  quint64 rid = client_->requestPosition();
  pending_[rid] = QStringLiteral("request_position");
  log(tr("请求持仓/资金 (id=%1)").arg(rid));
}

void LeftBottomTabs::onCancelOrder() {
  int row = activeTable_->currentRow();
  if (row < 0) {
    log(tr("请先在当前委托表选择一行"));
    return;
  }
  if (activeTdCombo_->count() == 0) {
    log(tr("无可用 TD 账户，请先连接并刷新 location"));
    return;
  }
  bool ok = false;
  // order_id 来自 Order 二进制推送，QString::number(quint64) 精确，回转 toULongLong 无损。
  quint64 oid = activeTable_->item(row, ColCOrderId)->text().toULongLong(&ok);
  if (!ok) {
    log(tr("无效订单号"));
    return;
  }
  QJsonObject td = activeTdCombo_->currentData().value<QJsonObject>();
  quint64 rid = client_->cancelOrder(td, oid);
  pending_[rid] = QStringLiteral("cancel_order");
  log(tr("发送撤单 order_id=%1 (id=%2)").arg(oid).arg(rid));
}

void LeftBottomTabs::onOrderReceived(const OrderInfo &o) {
  // ---- 订单历史（全量）----
  int hrow;
  auto hit = orderRowMap_.constFind(o.orderId);
  if (hit != orderRowMap_.cend())
    hrow = hit.value();
  else {
    hrow = orderTable_->rowCount();
    orderTable_->insertRow(hrow);
    orderRowMap_.insert(o.orderId, hrow);
  }
  auto setH = [&](int col, const QString &t) {
    auto *it = orderTable_->item(hrow, col);
    if (!it) {
      it = new QTableWidgetItem(t);
      orderTable_->setItem(hrow, col, it);
    } else {
      it->setText(t);
    }
  };
  setH(ColOOrderId, QString::number(o.orderId));
  setH(ColOInstrument, o.instrumentId);
  setH(ColOExchange, o.exchangeId);
  setH(ColOSide, o.side);
  setH(ColOOffset, o.offset);
  setH(ColOPrice, QString::number(o.limitPrice, 'f', 3));
  setH(ColOVolume, QString::number(o.volume));
  setH(ColOVolumeLeft, QString::number(o.volumeLeft));
  setH(ColOStatus, o.status);
  setH(ColOErrorMsg, o.errorMsg);
  if (auto *s = orderTable_->item(hrow, ColOStatus)) {
    if (o.status == QStringLiteral("Filled"))
      s->setForeground(QColor(Qt::darkGreen));
    else if (o.status == QStringLiteral("Cancelled") || o.status == QStringLiteral("Error"))
      s->setForeground(QColor(Qt::red));
    else
      s->setForeground(QColor(Qt::darkYellow));
  }
  orderTable_->resizeColumnsToContents();

  // ---- 当前委托（仅活动单）----
  if (isActive(o)) {
    int arow;
    auto ait = activeRowMap_.constFind(o.orderId);
    if (ait != activeRowMap_.cend())
      arow = ait.value();
    else {
      arow = activeTable_->rowCount();
      activeTable_->insertRow(arow);
      activeRowMap_.insert(o.orderId, arow);
    }
    auto setA = [&](int col, const QString &t) {
      auto *it = activeTable_->item(arow, col);
      if (!it) {
        it = new QTableWidgetItem(t);
        activeTable_->setItem(arow, col, it);
      } else {
        it->setText(t);
      }
    };
    setA(ColCOrderId, QString::number(o.orderId));
    setA(ColCInstrument, o.instrumentId);
    setA(ColCExchange, o.exchangeId);
    setA(ColCSide, o.side);
    setA(ColCOffset, o.offset);
    setA(ColCPrice, QString::number(o.limitPrice, 'f', 3));
    setA(ColCVolume, QString::number(o.volume));
    setA(ColCVolumeLeft, QString::number(o.volumeLeft));
    setA(ColCStatus, o.status);
    if (auto *s = activeTable_->item(arow, ColCStatus))
      s->setForeground(QColor(Qt::darkYellow));
    activeTable_->resizeColumnsToContents();
  } else {
    // 已终态：从当前委托移除
    auto ait = activeRowMap_.find(o.orderId);
    if (ait != activeRowMap_.end()) {
      int arow = ait.value();
      activeTable_->removeRow(arow);
      activeRowMap_.erase(ait);
      for (auto &v : activeRowMap_)
        if (v > arow) --v;
    }
  }
  log(tr("订单 %1 -> %2").arg(o.orderId).arg(o.status));
}

void LeftBottomTabs::onTradeReceived(const TradeInfo &t) {
  int row = tradeTable_->rowCount();
  tradeTable_->insertRow(row);
  tradeTable_->setItem(row, ColTTradeId, new QTableWidgetItem(QString::number(t.tradeId)));
  tradeTable_->setItem(row, ColTOrderId, new QTableWidgetItem(QString::number(t.orderId)));
  tradeTable_->setItem(row, ColTInstrument, new QTableWidgetItem(t.instrumentId));
  tradeTable_->setItem(row, ColTExchange, new QTableWidgetItem(t.exchangeId));
  tradeTable_->setItem(row, ColTSide, new QTableWidgetItem(t.side));
  tradeTable_->setItem(row, ColTOffset, new QTableWidgetItem(t.offset));
  tradeTable_->setItem(row, ColTPrice, new QTableWidgetItem(QString::number(t.price, 'f', 3)));
  tradeTable_->setItem(row, ColTVolume, new QTableWidgetItem(QString::number(t.volume)));
  tradeTable_->resizeColumnsToContents();
  log(tr("成交 %1 @ %2 x %3").arg(QString::number(t.tradeId)).arg(t.price).arg(t.volume));
}

void LeftBottomTabs::onPositionReceived(const PositionInfo &p) {
  QString key = QStringLiteral("%1|%2|%3|%4")
                    .arg(p.holderUid).arg(p.exchangeId, p.instrumentId, p.direction);
  int row;
  auto it = posRowMap_.constFind(key);
  if (it != posRowMap_.cend())
    row = it.value();
  else {
    row = posTable_->rowCount();
    posTable_->insertRow(row);
    posRowMap_.insert(key, row);
    for (int c = 0; c < ColPCount; ++c) posTable_->setItem(row, c, new QTableWidgetItem);
  }
  auto set = [&](int col, const QString &t) {
    if (auto *it = posTable_->item(row, col)) it->setText(t);
  };
  set(ColPInstrument, p.instrumentId);
  set(ColPExchange, p.exchangeId);
  set(ColPDirection, p.direction);
  set(ColPVolume, QString::number(p.volume));
  set(ColPYesterday, QString::number(p.yesterdayVolume));
  set(ColPFrozen, QString::number(p.frozenTotal));
  set(ColPAvgOpen, QString::number(p.avgOpenPrice, 'f', 3));
  set(ColPCost, QString::number(p.positionCostPrice, 'f', 3));
  set(ColPLast, QString::number(p.lastPrice, 'f', 3));
  set(ColPUnrealizedPnl, QString::number(p.unrealizedPnl, 'f', 2));
  set(ColPRealizedPnl, QString::number(p.realizedPnl, 'f', 2));
  set(ColPHolder, QString::number(p.holderUid));
  posTable_->resizeColumnsToContents();
}

void LeftBottomTabs::onAssetReceived(const AssetInfo &a) {
  int row;
  auto it = assetRowMap_.constFind(a.holderUid);
  if (it != assetRowMap_.cend())
    row = it.value();
  else {
    row = assetTable_->rowCount();
    assetTable_->insertRow(row);
    assetRowMap_.insert(a.holderUid, row);
    for (int c = 0; c < ColACount; ++c) assetTable_->setItem(row, c, new QTableWidgetItem);
  }
  auto set = [&](int col, const QString &t) {
    if (auto *it = assetTable_->item(row, col)) it->setText(t);
  };
  set(ColAHolder, QString::number(a.holderUid));
  set(ColACategory, a.ledgerCategory);
  set(ColADay, a.tradingDay);
  set(ColAInitEquity, QString::number(a.initialEquity, 'f', 2));
  set(ColAStaticEquity, QString::number(a.staticEquity, 'f', 2));
  set(ColADynamicEquity, QString::number(a.dynamicEquity, 'f', 2));
  set(ColAAvail, QString::number(a.avail, 'f', 2));
  set(ColAMarketValue, QString::number(a.marketValue, 'f', 2));
  set(ColAMargin, QString::number(a.margin, 'f', 2));
  set(ColAFrozenCash, QString::number(a.frozenCash, 'f', 2));
  set(ColAUnrealizedPnl, QString::number(a.unrealizedPnl, 'f', 2));
  set(ColARealizedPnl, QString::number(a.realizedPnl, 'f', 2));
  assetTable_->resizeColumnsToContents();
}

void LeftBottomTabs::onResponse(quint64 requestId, const QJsonValue &data, const QString &error) {
  auto it = pending_.find(requestId);
  if (it == pending_.end()) return;
  QString method = it.value();
  pending_.erase(it);
  if (!error.isEmpty()) {
    log(tr("%1 错误: %2").arg(method, error));
    return;
  }
  if (method == QStringLiteral("request_position"))
    log(tr("持仓/资金请求已发送"));
  else if (method == QStringLiteral("cancel_order"))
    log(tr("撤单已受理 action_id=%1")
            .arg(data.toObject().value("order_action_id").toVariant().toString()));
}

void LeftBottomTabs::appendLog(const QString &msg) {
  logView_->appendPlainText(msg);
}

}  // namespace kfclient
