#include "trade_model.h"
#include "utils/formatters.h"

namespace kf {

TradeModel::TradeModel(QObject *parent) : QAbstractTableModel(parent) {}

int TradeModel::rowCount(const QModelIndex &) const { return trades_.size(); }
int TradeModel::columnCount(const QModelIndex &) const { return ColCount; }

QVariant TradeModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= trades_.size()) return {};
    if (role != Qt::DisplayRole) return {};

    const auto &t = trades_[index.row()];
    switch (index.column()) {
    case ColTradeId:    return QString::number(t.trade_id);
    case ColOrderId:    return QString::number(t.order_id);
    case ColInstrument: return t.instrument_id;
    case ColExchange:   return t.exchange_id;
    case ColPrice:      return QString::number(t.price, 'f', 4);
    case ColVolume:     return t.volume;
    case ColSide:       return fmt::sideStr(t.side);
    case ColOffset:     return fmt::offsetStr(t.offset);
    case ColTime:       return fmt::nanoTimeStr(t.trade_time);
    }
    return {};
}

QVariant TradeModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case ColTradeId:    return QStringLiteral("成交号");
    case ColOrderId:    return QStringLiteral("订单号");
    case ColInstrument: return QStringLiteral("合约");
    case ColExchange:   return QStringLiteral("交易所");
    case ColPrice:      return QStringLiteral("价格");
    case ColVolume:     return QStringLiteral("数量");
    case ColSide:       return QStringLiteral("方向");
    case ColOffset:     return QStringLiteral("开平");
    case ColTime:       return QStringLiteral("时间");
    }
    return {};
}

void TradeModel::addTrade(const Trade &trade) {
    beginInsertRows(QModelIndex(), 0, 0);
    trades_.prepend(trade);
    endInsertRows();
}

void TradeModel::setTrades(const QVector<Trade> &trades) {
    beginResetModel();
    trades_ = trades;
    endResetModel();
}

} // namespace kf
