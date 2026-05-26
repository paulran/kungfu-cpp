#pragma once

#include "models/process_model.h"
#include <QWidget>
#include <QTableView>

namespace kf {

class SystemWidget : public QWidget {
    Q_OBJECT
public:
    explicit SystemWidget(ProcessModel *model, QWidget *parent = nullptr);

private:
    QTableView *table_;
};

} // namespace kf
