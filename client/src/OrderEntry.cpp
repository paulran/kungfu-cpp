// SPDX-License-Identifier: Apache-2.0

#include "OrderEntry.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace kfclient {

OrderEntry::OrderEntry(ApiClient *client, QWidget *parent)
    : QWidget(parent), client_(client), tabs_(new QTabWidget(this)) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(4);
  layout->addWidget(tabs_);

  buy_ = buildPage(tr("买入"), QStringLiteral("buyBtn"), QStringLiteral("Buy"));
  sell_ = buildPage(tr("卖出"), QStringLiteral("sellBtn"), QStringLiteral("Sell"));

  tabs_->addTab(buy_.page, tr("买入"));
  tabs_->addTab(sell_.page, tr("卖出"));

  // 自行连接 ApiClient 响应信号（与原 tab 模式一致）。
  connect(client_, &ApiClient::responseReceived, this, &OrderEntry::onResponse);
}

OrderEntry::Page OrderEntry::buildPage(const QString &btnText, const QString &objectName,
                                       const QString &side) {
  Page p;
  p.page = new QWidget(this);
  auto *v = new QVBoxLayout(p.page);
  v->setContentsMargins(8, 8, 8, 8);
  v->setSpacing(6);

  p.tdCombo = new QComboBox(p.page);
  p.exchange = new QLineEdit(QStringLiteral("SSE"), p.page);
  p.instrument = new QLineEdit(QStringLiteral("600000"), p.page);
  p.price = new QLineEdit(QStringLiteral("10.00"), p.page);
  p.volume = new QLineEdit(QStringLiteral("100"), p.page);
  p.offset = new QComboBox(p.page);
  p.offset->addItems({tr("Open"), tr("Close"), tr("CloseToday"), tr("CloseYesterday")});
  p.priceType = new QComboBox(p.page);
  p.priceType->addItems({tr("Limit"), tr("Any"), tr("FakBest5"), tr("ForwardBest"),
                         tr("ReverseBest"), tr("Fak"), tr("Fok")});

  // 紧凑布局：单字段行 = 标签 + 控件；双字段行 = 两组 标签+控件 并排。
  auto oneField = [page = p.page](const QString &label, QWidget *w) {
    auto *h = new QHBoxLayout();
    auto *l = new QLabel(label, page);
    l->setMinimumWidth(60);
    h->addWidget(l);
    h->addWidget(w, 1);
    return h;
  };
  auto twoField = [page = p.page](const QString &t1, QWidget *w1, const QString &t2,
                                  QWidget *w2) {
    auto *h = new QHBoxLayout();
    auto *l1 = new QLabel(t1, page);
    auto *l2 = new QLabel(t2, page);
    l1->setMinimumWidth(50);
    l2->setMinimumWidth(64);
    h->addWidget(l1);
    h->addWidget(w1, 1);
    h->addSpacing(8);
    h->addWidget(l2);
    h->addWidget(w2, 1);
    return h;
  };

  v->addLayout(oneField(tr("TD 账户"), p.tdCombo));
  v->addLayout(twoField(tr("交易所"), p.exchange, tr("合约"), p.instrument));
  v->addLayout(twoField(tr("价格"), p.price, tr("价格类型"), p.priceType));
  v->addLayout(twoField(tr("数量"), p.volume, tr("开平"), p.offset));
  v->addSpacing(4);

  p.btn = new QPushButton(btnText, p.page);
  p.btn->setObjectName(objectName);
  p.btn->setMinimumHeight(32);
  auto *btnRow = new QHBoxLayout();
  btnRow->addStretch();
  btnRow->addWidget(p.btn);
  v->addLayout(btnRow);
  v->addStretch();

  // 捕获 p（含指针的 struct）与 side；context=this 确保生命周期。
  connect(p.btn, &QPushButton::clicked, this, [this, p, side] { onIssue(p, side); });
  return p;
}

void OrderEntry::setLocations(const QJsonArray &locations) {
  locations_ = locations;
  auto fill = [](QComboBox *c, const QJsonArray &locs) {
    c->clear();
    for (const auto &v : locs) {
      QJsonObject o = v.toObject();
      if (o.value("category").toString() == QStringLiteral("td")) {
        QString label = QStringLiteral("%1/%2/%3 [%4]")
                            .arg(o.value("mode").toString(), o.value("group").toString(),
                                 o.value("name").toString(), o.value("uid").toString());
        c->addItem(label, o);
      }
    }
  };
  fill(buy_.tdCombo, locations);
  fill(sell_.tdCombo, locations);
}

void OrderEntry::onIssue(const Page &p, const QString &side) {
  if (p.tdCombo->count() == 0) {
    log(tr("无可用 TD 账户"));
    return;
  }
  QJsonObject tdLoc = p.tdCombo->currentData().value<QJsonObject>();
  QJsonObject fields;
  fields["exchange_id"] = p.exchange->text();
  fields["instrument_id"] = p.instrument->text();
  fields["limit_price"] = p.price->text().toDouble();
  fields["volume"] = p.volume->text().toLongLong();
  fields["side"] = side;
  fields["offset"] = p.offset->currentText();
  fields["price_type"] = p.priceType->currentText();
  fields["hedge_flag"] = QStringLiteral("Speculation");
  fields["volume_condition"] = QStringLiteral("Any");
  fields["time_condition"] = QStringLiteral("GFD");

  quint64 rid = client_->issueOrder(tdLoc, fields);
  pending_[rid] = QStringLiteral("issue_order");
  log(tr("%1 %2@%3 vol=%4 (id=%5)")
          .arg(side == QStringLiteral("Buy") ? tr("买入") : tr("卖出"), p.instrument->text(),
               p.exchange->text(), p.volume->text())
          .arg(rid));
}

void OrderEntry::onResponse(quint64 requestId, const QJsonValue & /*data*/, const QString &error) {
  auto it = pending_.find(requestId);
  if (it == pending_.end()) return;
  QString method = it.value();
  pending_.erase(it);
  if (!error.isEmpty()) {
    log(tr("%1 错误: %2").arg(method, error));
    return;
  }
  if (method == QStringLiteral("issue_order"))
    // order_id 在 JSON 响应中可能因 Qt 的 double 解析丢精度（order_id 常超 2^53），
    // 故此处不显示 id；精确订单号见左下"订单历史/当前委托"（来自二进制推送）。
    log(tr("订单已报入，详见订单历史/当前委托"));
}

}  // namespace kfclient
