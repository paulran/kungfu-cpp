#include "order_model.h"
#include "utils/formatters.h"

namespace kf {

OrderModel::OrderModel(QObject *parent) : QAbstractTableModel(parent) {}

int OrderModel::rowCount(const QModelIndex &) const { return orders_.size(); }
int OrderModel::columnCount(const QModelIndex &) const { return ColCount; }

QVariant OrderModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= orders_.size()) return {};
    if (role != Qt::DisplayRole) return {};

    const auto &o = orders_[index.row()];
    switch (index.column()) {
    case ColOrderId:    return QString::number(o.order_id);
    case ColInstrument: return o.instrument_id;
    case ColExchange:   return o.exchange_id;
    case ColSide:       return fmt::sideStr(o.side);
    case ColOffset:     return fmt::offsetStr(o.offset);
    case ColPrice:      return QString::number(o.limit_price, 'f', 4);
    case ColVolume:     return o.volume;
    case ColTraded:     return o.volume_traded;
    case ColLeft:       return o.volume_left;
    case ColStatus:     return fmt::orderStatusStr(o.status);
    case ColTime:       return fmt::nanoTimeStr(o.insert_time);
    }
    return {};
}

QVariant OrderModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case ColOrderId:    return QStringLiteral("订单号");
    case ColInstrument: return QStringLiteral("合约");
    case ColExchange:   return QStringLiteral("交易所");
    case ColSide:       return QStringLiteral("方向");
    case ColOffset:     return QStringLiteral("开平");
    case ColPrice:      return QStringLiteral("价格");
    case ColVolume:     return QStringLiteral("数量");
    case ColTraded:     return QStringLiteral("已成");
    case ColLeft:       return QStringLiteral("未成");
    case ColStatus:     return QStringLiteral("状态");
    case ColTime:       return QStringLiteral("时间");
    }
    return {};
}

void OrderModel::setOrders(const QVector<Order> &orders) {
    beginResetModel();
    orders_ = orders;
    endResetModel();
}

void OrderModel::updateOrder(const Order &order) {
    for (int i = 0; i < orders_.size(); ++i) {
        if (orders_[i].order_id == order.order_id) {
            orders_[i] = order;
            emit dataChanged(index(i, 0), index(i, ColCount - 1));
            return;
        }
    }
    beginInsertRows(QModelIndex(), orders_.size(), orders_.size());
    orders_.append(order);
    endInsertRows();
}

const Order *OrderModel::orderAt(int row) const {
    if (row >= 0 && row < orders_.size()) return &orders_[row];
    return nullptr;
}

} // namespace kf
