#include "order_entry_widget.h"
#include <QFormLayout>
#include <QVBoxLayout>
#include <QMessageBox>

namespace kf {

OrderEntryWidget::OrderEntryWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    auto *form = new QFormLayout;

    instrument_edit_ = new QLineEdit;
    instrument_edit_->setPlaceholderText(QStringLiteral("如 600000"));
    form->addRow(QStringLiteral("合约:"), instrument_edit_);

    exchange_combo_ = new QComboBox;
    exchange_combo_->addItems({"SSE", "SZSE", "CFFEX", "SHFE", "DCE", "CZCE", "INE", "GFEX"});
    form->addRow(QStringLiteral("交易所:"), exchange_combo_);

    price_spin_ = new QDoubleSpinBox;
    price_spin_->setRange(0, 99999999.99);
    price_spin_->setDecimals(4);
    price_spin_->setSingleStep(0.01);
    form->addRow(QStringLiteral("价格:"), price_spin_);

    volume_spin_ = new QSpinBox;
    volume_spin_->setRange(1, 999999);
    volume_spin_->setValue(100);
    form->addRow(QStringLiteral("数量:"), volume_spin_);

    side_combo_ = new QComboBox;
    side_combo_->addItem(QStringLiteral("买入"), static_cast<int>(Side::Buy));
    side_combo_->addItem(QStringLiteral("卖出"), static_cast<int>(Side::Sell));
    form->addRow(QStringLiteral("方向:"), side_combo_);

    offset_combo_ = new QComboBox;
    offset_combo_->addItem(QStringLiteral("开仓"), static_cast<int>(Offset::Open));
    offset_combo_->addItem(QStringLiteral("平仓"), static_cast<int>(Offset::Close));
    offset_combo_->addItem(QStringLiteral("平今"), static_cast<int>(Offset::CloseToday));
    offset_combo_->addItem(QStringLiteral("平昨"), static_cast<int>(Offset::CloseYesterday));
    form->addRow(QStringLiteral("开平:"), offset_combo_);

    price_type_combo_ = new QComboBox;
    price_type_combo_->addItem(QStringLiteral("限价"), static_cast<int>(PriceType::Limit));
    price_type_combo_->addItem(QStringLiteral("市价"), static_cast<int>(PriceType::Market));
    price_type_combo_->addItem(QStringLiteral("最优价"), static_cast<int>(PriceType::BestPrice));
    form->addRow(QStringLiteral("类型:"), price_type_combo_);

    layout->addLayout(form);

    submit_btn_ = new QPushButton(QStringLiteral("提交订单"));
    submit_btn_->setMinimumHeight(32);
    submit_btn_->setStyleSheet("QPushButton { background-color: #1890ff; color: white; font-weight: bold; }");
    layout->addWidget(submit_btn_);

    layout->addStretch();

    connect(submit_btn_, &QPushButton::clicked, this, [this]() {
        auto inst = instrument_edit_->text().trimmed();
        if (inst.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("请输入合约代码"));
            return;
        }

        OrderInput input;
        input.instrument_id = inst;
        input.exchange_id = exchange_combo_->currentText();
        input.limit_price = price_spin_->value();
        input.volume = volume_spin_->value();
        input.side = static_cast<Side>(side_combo_->currentData().toInt());
        input.offset = static_cast<Offset>(offset_combo_->currentData().toInt());
        input.price_type = static_cast<PriceType>(price_type_combo_->currentData().toInt());

        emit orderSubmitted(input);
    });
}

} // namespace kf
