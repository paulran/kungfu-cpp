// SPDX-License-Identifier: Apache-2.0

#include "OrderBook.h"

#include <QColor>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace kfclient {

namespace {
const QColor kAskBg("#fdecea");
const QColor kAskFg("#c0392b");
const QColor kBidBg("#eafaf1");
const QColor kBidFg("#27ae60");
}  // namespace

OrderBook::OrderBook(ApiClient *client, QWidget *parent)
    : QWidget(parent), client_(client),
      mdCombo_(new QComboBox(this)),
      exchangeEdit_(new QLineEdit(QStringLiteral("SSE"), this)),
      instrumentEdit_(new QLineEdit(QStringLiteral("600000"), this)),
      title_(new QLabel(tr("— 未订阅 —"), this)),
      lastLabel_(new QLabel(QStringLiteral("--"), this)),
      askBook_(new QTableWidget(5, 2, this)),
      bidBook_(new QTableWidget(5, 2, this)) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(4);

  // ---- 订阅表单（一行）：MD/交易所/合约三个输入框等分宽度 ----
  auto *form = new QHBoxLayout();
  form->addWidget(new QLabel(tr("MD:"), this));
  // 宽度提示只按 1 个字符算，避免长条目文本把 MD 框撑得远宽于其他输入框
  mdCombo_->setSizeAdjustPolicy(QComboBox::SizeAdjustPolicy::AdjustToMinimumContentsLengthWithIcon);
  mdCombo_->setMinimumContentsLength(1);
  form->addWidget(mdCombo_, 1);
  form->addWidget(new QLabel(tr("交易所"), this));
  form->addWidget(exchangeEdit_, 1);
  form->addWidget(new QLabel(tr("合约"), this));
  form->addWidget(instrumentEdit_, 1);
  subBtn_ = new QPushButton(tr("订阅"), this);
  subBtn_->setObjectName(QStringLiteral("primaryBtn"));
  cancelBtn_ = new QPushButton(tr("取消"), this);
  form->addWidget(subBtn_);
  form->addWidget(cancelBtn_);
  form->addStretch();
  layout->addLayout(form);

  // ---- 标题 ----
  title_->setObjectName(QStringLiteral("bookTitle"));
  title_->setAlignment(Qt::AlignCenter);
  layout->addWidget(title_);

  // ---- 卖盘 5 档（上）----
  askBook_->setVerticalHeaderLabels({tr("卖5"), tr("卖4"), tr("卖3"), tr("卖2"), tr("卖1")});
  askBook_->setObjectName(QStringLiteral("askBook"));
  askBook_->verticalHeader()->setDefaultAlignment(Qt::AlignCenter);
  askBook_->horizontalHeader()->setVisible(false);  // 隐藏"价格/数量"表头，紧凑显示
  askBook_->horizontalHeader()->setStretchLastSection(true);
  askBook_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  askBook_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  askBook_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  askBook_->setSelectionMode(QAbstractItemView::NoSelection);
  askBook_->setFocusPolicy(Qt::NoFocus);
  askBook_->verticalHeader()->setFixedWidth(34);
  for (int r = 0; r < 5; ++r)
    for (int c = 0; c < 2; ++c) {
      auto *it = new QTableWidgetItem;
      it->setBackground(kAskBg);
      it->setForeground(kAskFg);
      it->setTextAlignment(Qt::AlignCenter);
      askBook_->setItem(r, c, it);
    }
  layout->addWidget(askBook_);

  // ---- 最新价（中）----
  lastLabel_->setObjectName(QStringLiteral("lastPrice"));
  lastLabel_->setAlignment(Qt::AlignCenter);
  layout->addWidget(lastLabel_);

  // ---- 买盘 5 档（下）----
  bidBook_->setVerticalHeaderLabels({tr("买1"), tr("买2"), tr("买3"), tr("买4"), tr("买5")});
  bidBook_->setObjectName(QStringLiteral("bidBook"));
  bidBook_->verticalHeader()->setDefaultAlignment(Qt::AlignCenter);
  bidBook_->horizontalHeader()->setVisible(false);  // 隐藏"价格/数量"表头，紧凑显示
  bidBook_->horizontalHeader()->setStretchLastSection(true);
  bidBook_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  bidBook_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  bidBook_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  bidBook_->setSelectionMode(QAbstractItemView::NoSelection);
  bidBook_->setFocusPolicy(Qt::NoFocus);
  bidBook_->verticalHeader()->setFixedWidth(34);
  for (int r = 0; r < 5; ++r)
    for (int c = 0; c < 2; ++c) {
      auto *it = new QTableWidgetItem;
      it->setBackground(kBidBg);
      it->setForeground(kBidFg);
      it->setTextAlignment(Qt::AlignCenter);
      bidBook_->setItem(r, c, it);
    }
  layout->addWidget(bidBook_);
  layout->addStretch();

  connect(subBtn_, &QPushButton::clicked, this, [this] { onSubscribe(); });
  connect(cancelBtn_, &QPushButton::clicked, this, [this] { onCancel(); });

  // 自行连接 ApiClient 信号（与原 tab 模式一致）。
  connect(client_, &ApiClient::responseReceived, this, &OrderBook::onResponse);
  connect(client_, &ApiClient::quoteReceived, this, &OrderBook::onQuoteReceived);
}

void OrderBook::setLocations(const QJsonArray &locations) {
  locations_ = locations;
  mdCombo_->clear();
  for (const auto &v : locations) {
    QJsonObject o = v.toObject();
    if (o.value("category").toString() == QStringLiteral("md")) {
      QString label = QStringLiteral("%1/%2/%3 [%4]")
                          .arg(o.value("mode").toString(), o.value("group").toString(),
                               o.value("name").toString(), o.value("uid").toString());
      mdCombo_->addItem(label, o);
    }
  }
}

void OrderBook::clearBook() {
  for (int r = 0; r < 5; ++r) {
    if (auto *p = askBook_->item(r, 0)) p->setText(QString());
    if (auto *v = askBook_->item(r, 1)) v->setText(QString());
    if (auto *p = bidBook_->item(r, 0)) p->setText(QString());
    if (auto *v = bidBook_->item(r, 1)) v->setText(QString());
  }
  lastLabel_->setText(QStringLiteral("--"));
}

void OrderBook::onSubscribe() {
  if (mdCombo_->count() == 0) {
    log(tr("无 MD 源，请先连接并刷新 location"));
    return;
  }
  QJsonObject md = mdCombo_->currentData().value<QJsonObject>();
  QString ex = exchangeEdit_->text(), id = instrumentEdit_->text();
  if (ex.isEmpty() || id.isEmpty()) {
    log(tr("exchange / instrument 不能为空"));
    return;
  }
  quint64 rid = client_->requestMarketData(md, ex, id);
  pending_[rid] = QStringLiteral("request_market_data");
  displayKey_ = QStringLiteral("%1@%2").arg(id, ex);
  title_->setText(QStringLiteral("%1 @ %2").arg(id, ex));
  clearBook();
  log(tr("订阅行情 %1@%2 (id=%3)").arg(id, ex).arg(rid));
}

void OrderBook::onCancel() {
  if (mdCombo_->count() == 0) return;
  QJsonObject md = mdCombo_->currentData().value<QJsonObject>();
  QString ex = exchangeEdit_->text(), id = instrumentEdit_->text();
  quint64 rid = client_->cancelMarketData(md, ex, id);
  pending_[rid] = QStringLiteral("cancel_market_data");
  log(tr("取消行情 %1@%2 (id=%3)").arg(id, ex).arg(rid));
}

void OrderBook::onQuoteReceived(const QuoteInfo &quote) {
  QString key = QStringLiteral("%1@%2").arg(quote.instrumentId, quote.exchangeId);
  if (displayKey_.isEmpty() || key != displayKey_) return;

  // 卖盘：row i 显示 ask level (5-i) = askPrice[4-i]
  for (int i = 0; i < 5; ++i) {
    int idx = 4 - i;
    double price = quote.askPrice.size() > idx ? quote.askPrice[idx] : 0;
    qint64 vol = quote.askVolume.size() > idx ? quote.askVolume[idx] : 0;
    if (auto *p = askBook_->item(i, 0))
      p->setText(price > 0 ? QString::number(price, 'f', 3) : QString());
    if (auto *v = askBook_->item(i, 1))
      v->setText(vol > 0 ? QString::number(vol) : QString());
  }

  lastLabel_->setText(QString::number(quote.lastPrice, 'f', 3));

  // 买盘：row i 显示 bid level (i+1) = bidPrice[i]
  for (int i = 0; i < 5; ++i) {
    double price = quote.bidPrice.size() > i ? quote.bidPrice[i] : 0;
    qint64 vol = quote.bidVolume.size() > i ? quote.bidVolume[i] : 0;
    if (auto *p = bidBook_->item(i, 0))
      p->setText(price > 0 ? QString::number(price, 'f', 3) : QString());
    if (auto *v = bidBook_->item(i, 1))
      v->setText(vol > 0 ? QString::number(vol) : QString());
  }
}

void OrderBook::onResponse(quint64 requestId, const QJsonValue &data, const QString &error) {
  auto it = pending_.find(requestId);
  if (it == pending_.end()) return;
  QString method = it.value();
  pending_.erase(it);
  if (!error.isEmpty()) {
    log(tr("%1 错误: %2").arg(method, error));
    return;
  }
  if (method == QStringLiteral("request_market_data"))
    log(tr("已订阅, key=%1").arg(data.toObject().value("key").toVariant().toString()));
  else if (method == QStringLiteral("cancel_market_data"))
    log(tr("已取消订阅"));
}

}  // namespace kfclient
