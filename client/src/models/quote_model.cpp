#include "quote_model.h"
#include "utils/formatters.h"

namespace kf {

QuoteModel::QuoteModel(QObject *parent) : QAbstractTableModel(parent) {}

int QuoteModel::rowCount(const QModelIndex &) const { return quotes_.size(); }
int QuoteModel::columnCount(const QModelIndex &) const { return ColCount; }

QVariant QuoteModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= quotes_.size()) return {};
    if (role != Qt::DisplayRole) return {};

    const auto &q = quotes_[index.row()];
    switch (index.column()) {
    case ColInstrument: return q.instrument_id;
    case ColExchange:   return q.exchange_id;
    case ColLastPrice:  return QString::number(q.last_price, 'f', 4);
    case ColChange: {
        if (q.pre_close_price > 0) {
            double pct = (q.last_price - q.pre_close_price) / q.pre_close_price * 100.0;
            return QString::number(pct, 'f', 2) + "%";
        }
        return QStringLiteral("-");
    }
    case ColBid1:     return QString::number(q.bid_price[0], 'f', 4);
    case ColAsk1:     return QString::number(q.ask_price[0], 'f', 4);
    case ColVolume:   return q.volume;
    case ColTurnover: return QString::number(q.turnover / 10000.0, 'f', 2) + QStringLiteral("万");
    }
    return {};
}

QVariant QuoteModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case ColInstrument: return QStringLiteral("合约");
    case ColExchange:   return QStringLiteral("交易所");
    case ColLastPrice:  return QStringLiteral("最新价");
    case ColChange:     return QStringLiteral("涨跌%");
    case ColBid1:       return QStringLiteral("买一");
    case ColAsk1:       return QStringLiteral("卖一");
    case ColVolume:     return QStringLiteral("成交量");
    case ColTurnover:   return QStringLiteral("成交额");
    }
    return {};
}

void QuoteModel::updateQuote(const Quote &quote) {
    int idx = findQuote(quote.instrument_id);
    if (idx >= 0) {
        quotes_[idx] = quote;
        emit dataChanged(index(idx, 0), index(idx, ColCount - 1));
    } else {
        beginInsertRows(QModelIndex(), quotes_.size(), quotes_.size());
        quotes_.append(quote);
        endInsertRows();
    }
}

const Quote *QuoteModel::quoteAt(int row) const {
    if (row >= 0 && row < quotes_.size()) return &quotes_[row];
    return nullptr;
}

int QuoteModel::findQuote(const QString &instrument_id) const {
    for (int i = 0; i < quotes_.size(); ++i) {
        if (quotes_[i].instrument_id == instrument_id) return i;
    }
    return -1;
}

} // namespace kf
