#pragma once

#include "api/api_types.h"
#include <QAbstractTableModel>
#include <QVector>

namespace kf {

class QuoteModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { ColInstrument, ColExchange, ColLastPrice, ColChange,
                  ColBid1, ColAsk1, ColVolume, ColTurnover, ColCount };

    explicit QuoteModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void updateQuote(const Quote &quote);
    const Quote *quoteAt(int row) const;

private:
    QVector<Quote> quotes_;
    int findQuote(const QString &instrument_id) const;
};

} // namespace kf
