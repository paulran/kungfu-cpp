#include "position_model.h"
#include "utils/formatters.h"

namespace kf {

PositionModel::PositionModel(QObject *parent) : QAbstractTableModel(parent) {}

int PositionModel::rowCount(const QModelIndex &) const { return positions_.size(); }
int PositionModel::columnCount(const QModelIndex &) const { return ColCount; }

QVariant PositionModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= positions_.size()) return {};
    if (role != Qt::DisplayRole) return {};

    const auto &p = positions_[index.row()];
    switch (index.column()) {
    case ColInstrument:    return p.instrument_id;
    case ColExchange:      return p.exchange_id;
    case ColDirection:     return fmt::directionStr(p.direction);
    case ColVolume:        return p.volume;
    case ColYestVolume:    return p.yesterday_volume;
    case ColAvgPrice:      return QString::number(p.avg_open_price, 'f', 4);
    case ColUnrealizedPnl: return QString::number(p.unrealized_pnl, 'f', 2);
    case ColRealizedPnl:   return QString::number(p.realized_pnl, 'f', 2);
    }
    return {};
}

QVariant PositionModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case ColInstrument:    return QStringLiteral("合约");
    case ColExchange:      return QStringLiteral("交易所");
    case ColDirection:     return QStringLiteral("方向");
    case ColVolume:        return QStringLiteral("持仓");
    case ColYestVolume:    return QStringLiteral("昨仓");
    case ColAvgPrice:      return QStringLiteral("均价");
    case ColUnrealizedPnl: return QStringLiteral("浮动盈亏");
    case ColRealizedPnl:   return QStringLiteral("已实现盈亏");
    }
    return {};
}

void PositionModel::setPositions(const QVector<Position> &positions) {
    beginResetModel();
    positions_ = positions;
    endResetModel();
}

void PositionModel::updatePosition(const Position &pos) {
    for (int i = 0; i < positions_.size(); ++i) {
        if (positions_[i].instrument_id == pos.instrument_id &&
            positions_[i].exchange_id == pos.exchange_id &&
            positions_[i].direction == pos.direction) {
            positions_[i] = pos;
            emit dataChanged(index(i, 0), index(i, ColCount - 1));
            return;
        }
    }
    beginInsertRows(QModelIndex(), positions_.size(), positions_.size());
    positions_.append(pos);
    endInsertRows();
}

} // namespace kf
