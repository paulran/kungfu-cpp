#include "trade_widget.h"
#include <QVBoxLayout>
#include <QHeaderView>

namespace kf {

TradeWidget::TradeWidget(TradeModel *model, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);

    table_ = new QTableView;
    table_->setModel(model);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setAlternatingRowColors(true);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setDefaultSectionSize(24);
    table_->verticalHeader()->hide();
    layout->addWidget(table_);
}

} // namespace kf
