#include "BoardIoModel.h"

#include <utility>

namespace vb::qt {

BoardIoModel::BoardIoModel(std::vector<Row> rows, QObject* parent)
    : QAbstractListModel(parent), rows_(std::move(rows)) {}

int BoardIoModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant BoardIoModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.model() != this || index.column() != 0
        || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }

    const auto& row = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case Qt::DisplayRole:
    case ResourceRole:
        return row.resource;
    case AvailableRole:
        return row.available;
    case ActiveRole:
        return row.active;
    default:
        return {};
    }
}

QHash<int, QByteArray> BoardIoModel::roleNames() const {
    return {
        {ResourceRole, "resource"},
        {AvailableRole, "available"},
        {ActiveRole, "active"},
    };
}

QList<int> BoardIoModel::stageStates(std::span<const bool> states) {
    QList<int> changed;
    if (states.size() != rows_.size()) {
        return changed;
    }

    for (std::size_t row = 0; row < rows_.size(); ++row) {
        if (rows_[row].active != states[row]) {
            rows_[row].active = states[row];
            changed.append(static_cast<int>(row));
        }
    }
    return changed;
}

void BoardIoModel::publishChanges(const QList<int>& rows) {
    for (qsizetype begin = 0; begin < rows.size();) {
        qsizetype end = begin;
        while (end + 1 < rows.size() && rows[end + 1] == rows[end] + 1) {
            ++end;
        }
        emit dataChanged(index(rows[begin], 0), index(rows[end], 0), {ActiveRole});
        begin = end + 1;
    }
}

} // namespace vb::qt
