#include "order_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>

namespace kf {

OrderWidget::OrderWidget(OrderModel *model, QWidget *parent)
    : QWidget(parent)
    , model_(model)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);

    table_ = new QTableView;
    table_->setModel(model_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setAlternatingRowColors(true);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setDefaultSectionSize(24);
    table_->verticalHeader()->hide();
    layout->addWidget(table_);

    auto *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    cancel_btn_ = new QPushButton(QStringLiteral("撤单"));
    cancel_btn_->setStyleSheet("QPushButton { background-color: #ff4d4f; color: white; }");
    btnLayout->addWidget(cancel_btn_);
    layout->addLayout(btnLayout);

    connect(cancel_btn_, &QPushButton::clicked, this, [this]() {
        auto idx = table_->currentIndex();
        if (!idx.isValid()) {
            QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选择要撤销的订单"));
            return;
        }
        const auto *order = model_->orderAt(idx.row());
        if (!order) return;
        if (order->status == OrderStatus::Filled ||
            order->status == OrderStatus::Cancelled ||
            order->status == OrderStatus::Error) {
            QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("该订单无法撤销"));
            return;
        }
        emit cancelRequested(order->order_id);
    });
}

} // namespace kf
