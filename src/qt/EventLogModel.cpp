#include "qt/EventLogModel.h"

#include "qt/VirtualTime.h"

#include <cstdint>
#include <string_view>
#include <utility>

namespace vb::qt {

EventLogModel::EventLogModel(bool available, QObject* parent)
    : QAbstractListModel(parent), available_(available) {}

int EventLogModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant EventLogModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.model() != this || index.column() != 0
        || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const auto& event = events_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case KindRole:
        return static_cast<int>(event.kind);
    // Cycles exceed JavaScript's exact integer range; publish decimal text.
    case CycleRole:
        return QString::number(event.cycle);
    case TimeRole:
        return formatVirtualTime(event.cycle);
    case Qt::DisplayRole:
    case TextRole:
        return event.text;
    default:
        return {};
    }
}

QHash<int, QByteArray> EventLogModel::roleNames() const {
    return {
        {KindRole, "kind"},
        {CycleRole, "cycle"},
        {TimeRole, "time"},
        {TextRole, "text"},
    };
}

EventLogModel::Kind EventLogModel::kindOf(const QString& body) {
    if (body.startsWith(QLatin1String("SSEG"))) return Display;
    if (body.startsWith(QLatin1String("LED"))) return Leds;
    if (body.startsWith(QLatin1String("UART"))) return Uart;
    if (body.startsWith(QLatin1String("SW")) || body.startsWith(QLatin1String("BTN"))) return Inputs;
    return Other;
}

bool EventLogModel::parseLine(const std::string& line, Event& event) {
    static constexpr std::string_view prefix = "[cycle ";
    if (line.rfind(prefix, 0) != 0) return false;
    const auto close = line.find("] ", prefix.size());
    if (close == std::string::npos || close == prefix.size()) return false;
    uint64_t cycle = 0;
    for (auto i = prefix.size(); i < close; ++i) {
        const char digit = line[i];
        if (digit < '0' || digit > '9') return false;
        const auto value = static_cast<uint64_t>(digit - '0');
        if (cycle > (UINT64_MAX - value) / 10) return false;  // not a uint64 stamp
        cycle = cycle * 10 + value;
    }
    event.cycle = cycle;
    event.text = QString::fromStdString(line.substr(close + 2));
    event.kind = kindOf(event.text);
    return true;
}

void EventLogModel::setRecording(bool recording) {
    if (recording_ == recording) return;
    recording_ = recording;
    emit recordingChanged();
}

void EventLogModel::append(std::vector<Event> events) {
    if (events.empty()) return;
    recorded_ += static_cast<qint64>(events.size());
    // Events that would be discarded immediately are counted, never inserted.
    const std::size_t total = events_.size() + events.size();
    const std::size_t excess = total > MaximumEvents ? total - MaximumEvents : 0;
    std::size_t skip = 0;
    if (excess) {
        const std::size_t existing = std::min(excess, events_.size());
        if (existing) {
            beginRemoveRows({}, 0, static_cast<int>(existing) - 1);
            events_.erase(events_.begin(), events_.begin() + static_cast<std::ptrdiff_t>(existing));
            endRemoveRows();
        }
        skip = excess - existing;
        trimmed_ += static_cast<qint64>(excess);
    }
    const int first = count();
    beginInsertRows({}, first, first + static_cast<int>(events.size() - skip) - 1);
    for (std::size_t i = skip; i < events.size(); ++i) events_.push_back(std::move(events[i]));
    endInsertRows();
    emit countChanged();
}

void EventLogModel::clearEvents() {
    if (events_.empty() && !trimmed_) return;
    beginResetModel();
    events_.clear();
    trimmed_ = 0;
    endResetModel();
    emit countChanged();
}

EventFilterModel::EventFilterModel(EventLogModel* source, QObject* parent)
    : QSortFilterProxyModel(parent) {
    setSourceModel(source);
}

void EventFilterModel::refilter() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateRowsFilter();
#endif
    emit filterChanged();
}

void EventFilterModel::setFilterText(const QString& text) {
    if (filterText_ == text) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    filterText_ = text;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    emit filterChanged();
#else
    filterText_ = text;
    refilter();
#endif
}

void EventFilterModel::setShown(EventLogModel::Kind kind, bool shown) {
    if (shown_[kind] == shown) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    shown_[kind] = shown;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    emit filterChanged();
#else
    shown_[kind] = shown;
    refilter();
#endif
}

bool EventFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    // Nothing filtered: accept without reading the row (the common case while
    // a busy design records thousands of events per refresh).
    if (showDisplay() && showLeds() && showUart() && showInputs() && filterText_.trimmed().isEmpty())
        return true;
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    const auto kind = static_cast<EventLogModel::Kind>(index.data(EventLogModel::KindRole).toInt());
    if (kind != EventLogModel::Notice && !shown_[kind]) return false;
    const QString needle = filterText_.trimmed();
    if (needle.isEmpty()) return true;
    return index.data(EventLogModel::TextRole).toString().contains(needle, Qt::CaseInsensitive)
        || index.data(EventLogModel::CycleRole).toString().contains(needle);
}

} // namespace vb::qt
