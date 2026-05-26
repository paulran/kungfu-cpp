#pragma once

#include "models/order_model.h"
#include <QWidget>
#include <QTableView>
#include <QPushButton>

namespace kf {

class OrderWidget : public QWidget {
    Q_OBJECT
public:
    explicit OrderWidget(OrderModel *model, QWidget *parent = nullptr);

signals:
    void cancelRequested(uint64_t order_id);

private:
    QTableView *table_;
    QPushButton *cancel_btn_;
    OrderModel *model_;
};

} // namespace kf
