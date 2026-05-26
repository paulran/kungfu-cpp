#include "market_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>

namespace kf {

MarketWidget::MarketWidget(QuoteModel *model, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);

    auto *toolbar = new QHBoxLayout;
    instrument_edit_ = new QLineEdit;
    instrument_edit_->setPlaceholderText(QStringLiteral("合约代码"));
    instrument_edit_->setMaximumWidth(120);

    exchange_combo_ = new QComboBox;
    exchange_combo_->addItems({"SSE", "SZSE", "CFFEX", "SHFE", "DCE", "CZCE", "INE", "GFEX"});
    exchange_combo_->setMaximumWidth(80);

    subscribe_btn_ = new QPushButton(QStringLiteral("订阅"));
    subscribe_btn_->setMaximumWidth(60);

    toolbar->addWidget(instrument_edit_);
    toolbar->addWidget(exchange_combo_);
    toolbar->addWidget(subscribe_btn_);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    table_ = new QTableView;
    table_->setModel(model);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setAlternatingRowColors(true);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setDefaultSectionSize(24);
    table_->verticalHeader()->hide();
    layout->addWidget(table_);

    connect(subscribe_btn_, &QPushButton::clicked, this, [this]() {
        auto inst = instrument_edit_->text().trimmed();
        if (!inst.isEmpty()) {
            emit subscribeRequested(inst, exchange_combo_->currentText());
            instrument_edit_->clear();
        }
    });
    connect(instrument_edit_, &QLineEdit::returnPressed, subscribe_btn_, &QPushButton::click);
}

} // namespace kf
