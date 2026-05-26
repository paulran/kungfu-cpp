#pragma once

#include "api/api_types.h"
#include <QWidget>
#include <QComboBox>
#include <QLabel>

namespace kf {

class AssetWidget : public QWidget {
    Q_OBJECT
public:
    explicit AssetWidget(QWidget *parent = nullptr);

    void setAccounts(const QVector<AccountInfo> &accounts);

public slots:
    void setAsset(const kf::Asset &asset);

signals:
    void accountSelected(const QString &account_id);

private:
    QComboBox *account_combo_;
    QLabel *initial_equity_label_;
    QLabel *dynamic_equity_label_;
    QLabel *available_label_;
    QLabel *margin_label_;
    QLabel *frozen_label_;
    QLabel *realized_pnl_label_;
    QLabel *unrealized_pnl_label_;
};

} // namespace kf
