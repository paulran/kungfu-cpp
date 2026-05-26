#pragma once

#include "api/api_types.h"
#include <QAbstractTableModel>
#include <QVector>

namespace kf {

class OrderModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { ColOrderId, ColInstrument, ColExchange, ColSide, ColOffset,
                  ColPrice, ColVolume, ColTraded, ColLeft, ColStatus, ColTime, ColCount };

    explicit OrderModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void setOrders(const QVector<Order> &orders);
    void updateOrder(const Order &order);
    const Order *orderAt(int row) const;

private:
    QVector<Order> orders_;
};

} // namespace kf
