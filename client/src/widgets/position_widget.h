#pragma once

#include "api/api_types.h"
#include "models/position_model.h"
#include <QWidget>
#include <QTableView>
#include <QComboBox>

namespace kf {

class PositionWidget : public QWidget {
    Q_OBJECT
public:
    explicit PositionWidget(PositionModel *model, QWidget *parent = nullptr);

    void setAccounts(const QVector<AccountInfo> &accounts);

signals:
    void accountSelected(const QString &account_id);

private:
    QTableView *table_;
    QComboBox *account_combo_;
};

} // namespace kf
