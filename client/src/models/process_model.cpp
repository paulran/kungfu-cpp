#include "process_model.h"
#include "utils/formatters.h"

namespace kf {

ProcessModel::ProcessModel(QObject *parent) : QAbstractTableModel(parent) {}

int ProcessModel::rowCount(const QModelIndex &) const { return processes_.size(); }
int ProcessModel::columnCount(const QModelIndex &) const { return ColCount; }

QVariant ProcessModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= processes_.size()) return {};
    if (role != Qt::DisplayRole) return {};

    const auto &p = processes_[index.row()];
    switch (index.column()) {
    case ColName:     return p.name;
    case ColCategory: return fmt::categoryStr(p.category);
    case ColGroup:    return p.group;
    case ColMode:     return fmt::modeStr(p.mode);
    case ColState:    return fmt::brokerStateStr(p.broker_state);
    }
    return {};
}

QVariant ProcessModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case ColName:     return QStringLiteral("名称");
    case ColCategory: return QStringLiteral("类别");
    case ColGroup:    return QStringLiteral("分组");
    case ColMode:     return QStringLiteral("模式");
    case ColState:    return QStringLiteral("状态");
    }
    return {};
}

void ProcessModel::setProcesses(const QVector<ProcessInfo> &processes) {
    beginResetModel();
    processes_ = processes;
    endResetModel();
}

void ProcessModel::updateProcess(const ProcessInfo &info) {
    for (int i = 0; i < processes_.size(); ++i) {
        if (processes_[i].uid == info.uid) {
            processes_[i] = info;
            emit dataChanged(index(i, 0), index(i, ColCount - 1));
            return;
        }
    }
    beginInsertRows(QModelIndex(), processes_.size(), processes_.size());
    processes_.append(info);
    endInsertRows();
}

} // namespace kf
