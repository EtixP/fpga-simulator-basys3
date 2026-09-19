#include "SevenSegmentModel.h"

namespace vb::qt {

SevenSegmentModel::SevenSegmentModel(QObject* parent) : QAbstractListModel(parent) {}

int SevenSegmentModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : DigitCount;
}

QVariant SevenSegmentModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.model() != this || index.column() != 0
        || index.row() < 0 || index.row() >= DigitCount) {
        return {};
    }

    const auto& state = states_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case DigitRole:
        return index.row();
    case SegmentsRole:
        return state.segments;
    case DecimalPointRole:
        return state.decimalPoint;
    case Qt::DisplayRole:
    case CharacterRole:
        return state.character;
    default:
        return {};
    }
}

QHash<int, QByteArray> SevenSegmentModel::roleNames() const {
    return {
        {DigitRole, "digit"},
        {SegmentsRole, "segments"},
        {DecimalPointRole, "decimalPoint"},
        {CharacterRole, "character"},
    };
}

SevenSegmentModel::Changes SevenSegmentModel::stageStates(
    const std::array<State, DigitCount>& states) {
    Changes changes;
    for (std::size_t digit = 0; digit < states_.size(); ++digit) {
        const auto& next = states[digit];
        auto& current = states_[digit];
        auto& roles = changes[digit];
        if (current.segments != next.segments) {
            roles.append(SegmentsRole);
        }
        if (current.decimalPoint != next.decimalPoint) {
            roles.append(DecimalPointRole);
        }
        if (current.character != next.character) {
            roles.append(CharacterRole);
            roles.append(Qt::DisplayRole);
        }
        current = next;
    }
    return changes;
}

void SevenSegmentModel::publishChanges(const Changes& changes) {
    for (int begin = 0; begin < DigitCount;) {
        const auto& roles = changes[static_cast<std::size_t>(begin)];
        if (roles.isEmpty()) {
            ++begin;
            continue;
        }
        int end = begin;
        while (end + 1 < DigitCount
               && changes[static_cast<std::size_t>(end + 1)] == roles) {
            ++end;
        }
        emit dataChanged(index(begin, 0), index(end, 0), roles);
        begin = end + 1;
    }
}

} // namespace vb::qt
