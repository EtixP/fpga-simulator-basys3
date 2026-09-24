#include "qt/UartConsoleModel.h"

#include "qt/VirtualTime.h"

#include <algorithm>
#include <utility>

namespace vb::qt {

UartConsoleModel::UartConsoleModel(bool txBound, bool rxBound, QObject* parent)
    : QAbstractListModel(parent), txBound_(txBound), rxBound_(rxBound) {}

int UartConsoleModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant UartConsoleModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.model() != this || index.column() != 0
        || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }

    const auto& line = lines_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case KindRole:
        return static_cast<int>(line.kind);
    case Qt::DisplayRole:
    case TextRole:
        return line.text;
    case HexRole:
        return line.hex;
    case ByteCountRole:
        return static_cast<int>(line.bytes.size());
    // Cycles exceed JavaScript's exact integer range; publish decimal text.
    case CycleRole:
        return QString::number(line.firstCycle);
    case LastCycleRole:
        return QString::number(line.lastCycle);
    case TimeRole:
        return formatVirtualTime(line.firstCycle);
    default:
        return {};
    }
}

QHash<int, QByteArray> UartConsoleModel::roleNames() const {
    return {
        {KindRole, "kind"},
        {TextRole, "text"},
        {HexRole, "hex"},
        {ByteCountRole, "byteCount"},
        {CycleRole, "cycle"},
        {LastCycleRole, "lastCycle"},
        {TimeRole, "time"},
    };
}

QString UartConsoleModel::displayText(const QByteArray& bytes) {
    qsizetype end = bytes.size();
    // A newline ends the row; a carriage return directly before it is part of
    // the same CRLF terminator. Both remain visible in the hex view.
    if (end > 0 && bytes.at(end - 1) == '\n') {
        --end;
        if (end > 0 && bytes.at(end - 1) == '\r') --end;
    }
    QString text;
    text.reserve(end);
    for (qsizetype i = 0; i < end; ++i) {
        const auto value = static_cast<uint8_t>(bytes.at(i));
        if (value >= 0x20 && value < 0x7F) {
            text.append(QLatin1Char(static_cast<char>(value)));
        } else if (value == '\t') {
            text.append(QStringLiteral("\\t"));
        } else if (value == '\r') {
            text.append(QStringLiteral("\\r"));
        } else {
            text.append(QStringLiteral("\\x")
                        + QString::number(value, 16).rightJustified(2, QLatin1Char('0')).toUpper());
        }
    }
    return text;
}

QString UartConsoleModel::hexText(const QByteArray& bytes) {
    return QString::fromLatin1(bytes.toHex(' ').toUpper());
}

bool UartConsoleModel::stageCounters(qint64 txBytes, qint64 rxBytes, qint64 pendingRxBytes,
                                     qint64 framingErrors) {
    const bool changed = txBytes_ != txBytes || rxBytes_ != rxBytes
        || pendingRxBytes_ != pendingRxBytes || framingErrors_ != framingErrors;
    txBytes_ = txBytes;
    rxBytes_ = rxBytes;
    pendingRxBytes_ = pendingRxBytes;
    framingErrors_ = framingErrors;
    return changed;
}

void UartConsoleModel::publishCounters(bool changed) {
    if (changed) emit trafficChanged();
}

void UartConsoleModel::appendTraffic(Kind kind, std::span<const Byte> bytes) {
    if (bytes.empty() || kind == Notice) return;
    // Only the final row can continue, and only with the same direction.
    Line* open = openLine_ && !lines_.empty() && lines_.back().kind == kind
        ? &lines_.back() : nullptr;
    Line* const previous = open;
    bool extended = false;
    std::deque<Line> added;  // push_back keeps references to earlier elements
    for (const Byte& byte : bytes) {
        if (!open || open->terminated || open->bytes.size() >= MaximumLineBytes) {
            added.push_back({kind, {}, false, byte.cycle, byte.cycle, {}, {}});
            open = &added.back();
        } else if (open == previous) {
            extended = true;
        }
        open->bytes.append(static_cast<char>(byte.value));
        open->lastCycle = byte.cycle;
        open->terminated = byte.value == '\n';
    }
    for (auto& line : added) {
        line.text = displayText(line.bytes);
        line.hex = hexText(line.bytes);
    }
    if (extended) {
        previous->text = displayText(previous->bytes);
        previous->hex = hexText(previous->bytes);
        const int row = count() - 1;
        emit dataChanged(index(row, 0), index(row, 0),
                         {TextRole, HexRole, ByteCountRole, LastCycleRole});
    }
    insertLines(std::move(added));
    openLine_ = true;
}

void UartConsoleModel::appendNotice(const QString& text, uint64_t cycle) {
    if (!available()) return;
    std::deque<Line> added;
    added.push_back({Notice, {}, true, cycle, cycle, text, {}});
    insertLines(std::move(added));
    openLine_ = false;
}

void UartConsoleModel::insertLines(std::deque<Line> added) {
    if (added.empty()) return;
    // Rows that would be trimmed immediately are counted but never inserted.
    const std::size_t total = lines_.size() + added.size();
    const std::size_t excess = total > MaximumLines ? total - MaximumLines : 0;
    if (excess) {
        const std::size_t existing = std::min(excess, lines_.size());
        if (existing) {
            beginRemoveRows({}, 0, static_cast<int>(existing) - 1);
            lines_.erase(lines_.begin(), lines_.begin() + static_cast<std::ptrdiff_t>(existing));
            endRemoveRows();
        }
        added.erase(added.begin(), added.begin() + static_cast<std::ptrdiff_t>(excess - existing));
        trimmedLines_ += static_cast<qint64>(excess);
    }
    const int first = count();
    beginInsertRows({}, first, first + static_cast<int>(added.size()) - 1);
    for (auto& line : added) lines_.push_back(std::move(line));
    endInsertRows();
    emit countChanged();
}

void UartConsoleModel::setSendError(const QString& message) {
    if (sendError_ == message) return;
    sendError_ = message;
    emit sendErrorChanged();
}

void UartConsoleModel::clearLines() {
    openLine_ = false;
    if (lines_.empty() && !trimmedLines_) return;
    beginResetModel();
    lines_.clear();
    trimmedLines_ = 0;
    endResetModel();
    emit countChanged();
}

} // namespace vb::qt
