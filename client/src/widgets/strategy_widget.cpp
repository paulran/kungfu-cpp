#include "strategy_widget.h"
#include "utils/formatters.h"
#include <QVBoxLayout>
#include <QHeaderView>

namespace kf {

StrategyWidget::StrategyWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);

    table_ = new QTableWidget;
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({
        QStringLiteral("名称"),
        QStringLiteral("分组"),
        QStringLiteral("状态")
    });
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setAlternatingRowColors(true);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setDefaultSectionSize(24);
    table_->verticalHeader()->hide();
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_);
}

void StrategyWidget::setStrategies(const QVector<StrategyInfo> &strategies) {
    table_->setRowCount(strategies.size());
    for (int i = 0; i < strategies.size(); ++i) {
        const auto &s = strategies[i];
        table_->setItem(i, 0, new QTableWidgetItem(s.name));
        table_->setItem(i, 1, new QTableWidgetItem(s.group));
        table_->setItem(i, 2, new QTableWidgetItem(fmt::brokerStateStr(s.state)));
    }
}

} // namespace kf
