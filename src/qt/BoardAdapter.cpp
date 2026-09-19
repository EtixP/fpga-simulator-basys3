#include "qt/BoardAdapter.h"

#include "board/BoardModel.h"

#include <QScopedValueRollback>
#include <QThread>

#include <array>
#include <vector>

namespace vb::qt {
namespace {
QStringList numberedNames(const QString& prefix, int count) {
    QStringList names;
    for (int i = 0; i < count; ++i) names.append(prefix + QString::number(i));
    return names;
}

template <typename HasResource>
std::vector<BoardIoModel::Row> ioRows(const QStringList& names, HasResource has) {
    std::vector<BoardIoModel::Row> rows;
    rows.reserve(names.size());
    for (int i = 0; i < names.size(); ++i)
        rows.push_back({names.at(i), has(i), false});
    return rows;
}

QStringList buttonNames() {
    QStringList names;
    for (const char* name : kButtonNames) names.append(QString::fromLatin1(name));
    return names;
}
}  // namespace

BoardAdapter::BoardAdapter(BoardModel* board, QObject* parent)
    : QObject(parent), board_(board), ownerThread_(QThread::currentThread()),
      hasDisplay_(board && board->hasDisplay()),
      switches_(new BoardIoModel(ioRows(numberedNames(QStringLiteral("SW"),
          BoardModel::kSwitchCount), [board](int i) {
              return board && board->hasSwitch(i);
          }), this)),
      leds_(new BoardIoModel(ioRows(numberedNames(QStringLiteral("LED"),
          BoardModel::kLedCount), [board](int i) {
              return board && board->hasLed(i);
          }), this)),
      buttons_(new BoardIoModel(ioRows(buttonNames(), [board](int i) {
          return board && board->hasButton(static_cast<Button>(i));
      }), this)),
      digits_(new SevenSegmentModel(this)) {
    refresh();
}

bool BoardAdapter::canAccessBoard() const {
    // Check the thread before accessing any mutable board or presentation state.
    return QThread::currentThread() == ownerThread_ && thread() == ownerThread_
        && !publishing_;
}

bool BoardAdapter::setSwitch(int index, bool on) {
    if (!canAccessBoard() || !board_ || index < 0
        || index >= static_cast<int>(BoardModel::kSwitchCount)
        || !board_->hasSwitch(index)) return false;
    board_->setSwitch(index, on);
    return refresh();
}

bool BoardAdapter::setButton(int index, bool pressed) {
    if (!canAccessBoard() || !board_ || index < 0
        || index >= static_cast<int>(kButtonNames.size())
        || !board_->hasButton(static_cast<Button>(index))) return false;
    board_->setButton(static_cast<Button>(index), pressed);
    return refresh();
}

bool BoardAdapter::refresh() {
    if (!canAccessBoard()) return false;
    QScopedValueRollback<bool> publishing(publishing_, true);
    std::array<bool, BoardModel::kSwitchCount> switches{};
    std::array<bool, BoardModel::kLedCount> leds{};
    std::array<bool, kButtonNames.size()> buttons{};
    std::array<SevenSegmentModel::State, SevenSegmentModel::DigitCount> digits{};
    if (board_) {
        for (uint32_t i = 0; i < switches.size(); ++i)
            switches[i] = board_->switchState(i);
        for (uint32_t i = 0; i < leds.size(); ++i)
            leds[i] = board_->ledState(i);
        for (uint32_t i = 0; i < buttons.size(); ++i)
            buttons[i] = board_->buttonState(static_cast<Button>(i));
        for (uint32_t i = 0; i < digits.size(); ++i) {
            digits[i] = {board_->digitSegments(i), board_->digitDp(i),
                         QString(QChar::fromLatin1(board_->digitChar(i)))};
        }
    }

    const auto switchChanges = switches_->stageStates(switches);
    const auto ledChanges = leds_->stageStates(leds);
    const auto buttonChanges = buttons_->stageStates(buttons);
    const auto digitChanges = digits_->stageStates(digits);
    switches_->publishChanges(switchChanges);
    leds_->publishChanges(ledChanges);
    buttons_->publishChanges(buttonChanges);
    digits_->publishChanges(digitChanges);
    return true;
}

}  // namespace vb::qt
