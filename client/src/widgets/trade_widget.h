#pragma once

#include "models/trade_model.h"
#include <QWidget>
#include <QTableView>

namespace kf {

class TradeWidget : public QWidget {
    Q_OBJECT
public:
    explicit TradeWidget(TradeModel *model, QWidget *parent = nullptr);

private:
    QTableView *table_;
};

} // namespace kf
