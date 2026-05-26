#pragma once

#include "api/api_types.h"
#include <QWidget>
#include <QTableWidget>

namespace kf {

class StrategyWidget : public QWidget {
    Q_OBJECT
public:
    explicit StrategyWidget(QWidget *parent = nullptr);

    void setStrategies(const QVector<StrategyInfo> &strategies);

private:
    QTableWidget *table_;
};

} // namespace kf
