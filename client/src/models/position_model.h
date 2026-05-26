#pragma once

#include "api/api_types.h"
#include <QAbstractTableModel>
#include <QVector>

namespace kf {

class PositionModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { ColInstrument, ColExchange, ColDirection, ColVolume,
                  ColYestVolume, ColAvgPrice, ColUnrealizedPnl, ColRealizedPnl, ColCount };

    explicit PositionModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void setPositions(const QVector<Position> &positions);
    void updatePosition(const Position &pos);

private:
    QVector<Position> positions_;
};

} // namespace kf
