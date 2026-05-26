#pragma once

#include "models/quote_model.h"
#include <QWidget>
#include <QTableView>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>

namespace kf {

class MarketWidget : public QWidget {
    Q_OBJECT
public:
    explicit MarketWidget(QuoteModel *model, QWidget *parent = nullptr);

signals:
    void subscribeRequested(const QString &instrument_id, const QString &exchange_id);

private:
    QTableView *table_;
    QLineEdit *instrument_edit_;
    QComboBox *exchange_combo_;
    QPushButton *subscribe_btn_;
};

} // namespace kf
