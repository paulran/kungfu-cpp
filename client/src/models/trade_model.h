#pragma once

#include "api/api_types.h"
#include <QAbstractTableModel>
#include <QVector>

namespace kf {

class TradeModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { ColTradeId, ColOrderId, ColInstrument, ColExchange,
                  ColPrice, ColVolume, ColSide, ColOffset, ColTime, ColCount };

    explicit TradeModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void addTrade(const Trade &trade);
    void setTrades(const QVector<Trade> &trades);

private:
    QVector<Trade> trades_;
};

} // namespace kf
