#include "position_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QHeaderView>

namespace kf {

PositionWidget::PositionWidget(PositionModel *model, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);

    auto *toolbar = new QHBoxLayout;
    toolbar->addWidget(new QLabel(QStringLiteral("账户:")));
    account_combo_ = new QComboBox;
    account_combo_->setMinimumWidth(120);
    toolbar->addWidget(account_combo_);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    table_ = new QTableView;
    table_->setModel(model);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setAlternatingRowColors(true);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setDefaultSectionSize(24);
    table_->verticalHeader()->hide();
    layout->addWidget(table_);

    connect(account_combo_, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        if (!text.isEmpty()) emit accountSelected(text);
    });
}

void PositionWidget::setAccounts(const QVector<AccountInfo> &accounts) {
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

} // namespace kf
