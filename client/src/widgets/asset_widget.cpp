#include "asset_widget.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QGroupBox>

namespace kf {

AssetWidget::AssetWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    auto *toolbar = new QHBoxLayout;
    toolbar->addWidget(new QLabel(QStringLiteral("账户:")));
    account_combo_ = new QComboBox;
    account_combo_->setMinimumWidth(120);
    toolbar->addWidget(account_combo_);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    auto *form = new QFormLayout;
    initial_equity_label_ = new QLabel("-");
    dynamic_equity_label_ = new QLabel("-");
    available_label_ = new QLabel("-");
    margin_label_ = new QLabel("-");
    frozen_label_ = new QLabel("-");
    realized_pnl_label_ = new QLabel("-");
    unrealized_pnl_label_ = new QLabel("-");

    form->addRow(QStringLiteral("初始权益:"), initial_equity_label_);
    form->addRow(QStringLiteral("动态权益:"), dynamic_equity_label_);
    form->addRow(QStringLiteral("可用资金:"), available_label_);
    form->addRow(QStringLiteral("保证金:"), margin_label_);
    form->addRow(QStringLiteral("冻结资金:"), frozen_label_);
    form->addRow(QStringLiteral("已实现盈亏:"), realized_pnl_label_);
    form->addRow(QStringLiteral("浮动盈亏:"), unrealized_pnl_label_);
    layout->addLayout(form);

    layout->addStretch();

    connect(account_combo_, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        if (!text.isEmpty()) emit accountSelected(text);
    });
}

void AssetWidget::setAccounts(const QVector<AccountInfo> &accounts) {
    auto current = account_combo_->currentText();
    account_combo_->blockSignals(true);
    account_combo_->clear();
    for (const auto &a : accounts) {
        account_combo_->addItem(a.account_id);
    }
    if (!current.isEmpty()) {
        int idx = account_combo_->findText(current);
        if (idx >= 0) account_combo_->setCurrentIndex(idx);
    }
    account_combo_->blockSignals(false);
}

void AssetWidget::setAsset(const Asset &asset) {
    initial_equity_label_->setText(QString::number(asset.initial_equity, 'f', 2));
    dynamic_equity_label_->setText(QString::number(asset.dynamic_equity, 'f', 2));
    available_label_->setText(QString::number(asset.available, 'f', 2));
    margin_label_->setText(QString::number(asset.margin, 'f', 2));
    frozen_label_->setText(QString::number(asset.frozen_cash + asset.frozen_margin + asset.frozen_fee, 'f', 2));

    auto setPnlStyle = [](QLabel *label, double val) {
        label->setText(QString::number(val, 'f', 2));
        if (val > 0) label->setStyleSheet("color: red;");
        else if (val < 0) label->setStyleSheet("color: green;");
        else label->setStyleSheet("");
    };
    setPnlStyle(realized_pnl_label_, asset.realized_pnl);
    setPnlStyle(unrealized_pnl_label_, asset.unrealized_pnl);
}

} // namespace kf
