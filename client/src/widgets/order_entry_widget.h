#pragma once

#include "api/api_types.h"
#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>

namespace kf {

class OrderEntryWidget : public QWidget {
    Q_OBJECT
public:
    explicit OrderEntryWidget(QWidget *parent = nullptr);

signals:
    void orderSubmitted(const kf::OrderInput &input);

private:
    QLineEdit *instrument_edit_;
    QComboBox *exchange_combo_;
    QDoubleSpinBox *price_spin_;
    QSpinBox *volume_spin_;
    QComboBox *side_combo_;
    QComboBox *offset_combo_;
    QComboBox *price_type_combo_;
    QPushButton *submit_btn_;
};

} // namespace kf
