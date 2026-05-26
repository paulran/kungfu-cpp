#pragma once

#include "api/api_types.h"
#include <QAbstractTableModel>
#include <QVector>

namespace kf {

class ProcessModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { ColName, ColCategory, ColGroup, ColMode, ColState, ColCount };

    explicit ProcessModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void setProcesses(const QVector<ProcessInfo> &processes);
    void updateProcess(const ProcessInfo &info);

private:
    QVector<ProcessInfo> processes_;
};

} // namespace kf
