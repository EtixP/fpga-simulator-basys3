#include "qt/SignalInspectorModel.h"

#include "qt/VirtualTime.h"

#include <algorithm>
#include <utility>

namespace vb::qt {

SignalInspectorModel::SignalInspectorModel(bool available, QObject* parent)
    : QAbstractListModel(parent), available_(available) {}

int SignalInspectorModel::watchCount() const {
    return static_cast<int>(std::count_if(rows_.begin(), rows_.end(),
                                          [](const Row& row) { return row.watch; }));
}

QString SignalInspectorModel::snapshotCycleText() const {
    return haveSnapshot_ ? QString::number(snapshotCycle_) : QString();
}

QString SignalInspectorModel::snapshotTimeText() const {
    return haveSnapshot_ ? formatVirtualTime(snapshotCycle_) : QString();
}

int SignalInspectorModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QString SignalInspectorModel::valueText(uint64_t value, int width) {
    if (width <= 1) return QString::number(value & 1);
    const int digits = (width + 3) / 4;
    return QStringLiteral("0x") + QString::number(value, 16).rightJustified(digits, QLatin1Char('0')).toUpper();
}

QVariant SignalInspectorModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.model() != this || index.column() != 0
        || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const auto& row = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return row.name;
    case KindRole:
        return static_cast<int>(row.kind);
    case WidthRole:
        return row.width;
    case RangeRole:
        return row.range;
    // Values exceed JavaScript's exact integer range; publish text.
    case ValueRole:
        return valueText(row.value, row.width);
    case DecimalRole:
        return QString::number(row.value);
    case BindingRole:
        return row.binding;
    case ChangedRole:
        return row.changed;
    case WatchRole:
        return row.watch;
    default:
        return {};
    }
}

QHash<int, QByteArray> SignalInspectorModel::roleNames() const {
    return {
        {NameRole, "name"},
        {KindRole, "kind"},
        {WidthRole, "bits"},
        {RangeRole, "range"},
        {ValueRole, "value"},
        {DecimalRole, "decimal"},
        {BindingRole, "binding"},
        {ChangedRole, "changed"},
        {WatchRole, "watch"},
    };
}

void SignalInspectorModel::resetRows(std::vector<Row> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    haveSnapshot_ = false;
    endResetModel();
    emit countChanged();
}

SignalInspectorModel::Changes SignalInspectorModel::stageValues(const std::vector<uint64_t>& values,
                                                                uint64_t cycle) {
    Changes changes;
    if (values.size() != rows_.size()) return changes;
    const bool first = !haveSnapshot_;
    changes.snapshotMoved = first || snapshotCycle_ != cycle;
    int begin = -1;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        auto& row = rows_[i];
        const bool changed = !first && row.value != values[i];
        const bool dirty = row.value != values[i] || row.changed != changed;
        row.value = values[i];
        row.changed = changed;
        const int index = static_cast<int>(i);
        if (dirty && begin < 0) begin = index;
        if (!dirty && begin >= 0) {
            changes.ranges.emplace_back(begin, index - 1);
            begin = -1;
        }
    }
    if (begin >= 0) changes.ranges.emplace_back(begin, count() - 1);
    haveSnapshot_ = true;
    snapshotCycle_ = cycle;
    return changes;
}

void SignalInspectorModel::publishValues(const Changes& changes) {
    for (const auto& [first, last] : changes.ranges)
        emit dataChanged(index(first, 0), index(last, 0), {ValueRole, DecimalRole, ChangedRole});
    if (changes.snapshotMoved) emit snapshotChanged();
}

void SignalInspectorModel::appendWatch(Row row) {
    const int at = count();
    beginInsertRows({}, at, at);
    rows_.push_back(std::move(row));
    endInsertRows();
    emit countChanged();
}

void SignalInspectorModel::removeRow(int row) {
    if (row < 0 || row >= count()) return;
    beginRemoveRows({}, row, row);
    rows_.erase(rows_.begin() + row);
    endRemoveRows();
    emit countChanged();
}

void SignalInspectorModel::setWatchError(const QString& message) {
    if (watchError_ == message) return;
    watchError_ = message;
    emit watchErrorChanged();
}

SignalFilterModel::SignalFilterModel(SignalInspectorModel* source, QObject* parent)
    : QSortFilterProxyModel(parent) {
    setSourceModel(source);
    setFilterRole(SignalInspectorModel::NameRole);
}

void SignalFilterModel::setFilterText(const QString& text) {
    if (filterText_ == text) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    filterText_ = text;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    filterText_ = text;
    invalidateRowsFilter();
#endif
    emit filterTextChanged();
}

bool SignalFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    const QString needle = filterText_.trimmed();
    if (needle.isEmpty()) return true;
    const QString name = sourceModel()->index(sourceRow, 0, sourceParent)
                             .data(SignalInspectorModel::NameRole).toString();
    return name.contains(needle, Qt::CaseInsensitive);
}

} // namespace vb::qt
