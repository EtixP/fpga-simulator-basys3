#include "qt/BoardAdapter.h"

#include "board/BoardModel.h"

#include <QScopedValueRollback>
#include <QThread>

#include <algorithm>
#include <array>
#include <map>
#include <utility>
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
      digits_(new SevenSegmentModel(this)),
      uart_(new UartConsoleModel(board && board->hasUartTx(),
                                 board && board->hasUartRx(), this)),
      vga_(new VgaFrameModel(board && board->hasVga(), this)),
      inspector_(new SignalInspectorModel(board != nullptr, this)),
      inspectorView_(new SignalFilterModel(inspector_, this)),
      eventLog_(new EventLogModel(board != nullptr, this)),
      eventLogView_(new EventFilterModel(eventLog_, this)) {
    buildInspector();
    refresh();
}

namespace {
// "LED0–LED15" for consecutive resources on consecutive bits, else a list.
QString bindingSummary(std::vector<std::pair<uint32_t, QString>> resources) {
    if (resources.empty()) return {};
    std::sort(resources.begin(), resources.end());
    const auto split = [](const QString& name, QString& stem, int& number) {
        qsizetype digits = name.size();
        while (digits > 0 && name.at(digits - 1).isDigit()) --digits;
        stem = name.left(digits);
        bool ok = false;
        number = name.mid(digits).toInt(&ok);
        return ok;
    };
    QString firstStem;
    int firstNumber = 0;
    bool consecutive = resources.size() > 1 && split(resources.front().second, firstStem, firstNumber);
    for (std::size_t i = 1; consecutive && i < resources.size(); ++i) {
        QString stem;
        int number = 0;
        consecutive = split(resources[i].second, stem, number) && stem == firstStem
            && number == firstNumber + static_cast<int>(i)
            && resources[i].first == resources.front().first + i;
    }
    if (consecutive)
        return resources.front().second + QStringLiteral("–") + resources.back().second;
    QStringList names;
    for (const auto& resource : resources) names.append(resource.second);
    return names.join(QStringLiteral(", "));
}
}  // namespace

BoardAdapter::~BoardAdapter() {
    if (!board_ || !eventLog_->recording()) return;
    board_->clearLog();  // this view's undrained lines are not the board owner's
    board_->setLogEnabled(logWasEnabled_);
}

void BoardAdapter::buildInspector() {
    if (!board_) return;
    std::map<SignalId, std::vector<std::pair<uint32_t, QString>>> bound;
    SignalId clock = kNoSignal;
    for (const auto& [resource, signal] : board_->binding().resources()) {
        bound[signal.id].emplace_back(signal.bit, QString::fromStdString(resource));
        if (resource == "CLK100") clock = signal.id;
    }
    std::vector<SignalInspectorModel::Row> rows;
    for (const auto& info : board_->designPorts()) {
        const SignalId id = board_->findSignal(info.name);
        if (id == kNoSignal) continue;
        SignalInspectorModel::Row row;
        row.name = QString::fromStdString(info.name);
        row.kind = id == clock ? SignalInspectorModel::Clock
            : info.input ? SignalInspectorModel::Input : SignalInspectorModel::Output;
        row.width = static_cast<int>(info.width);
        if (info.packedRange)
            row.range = QStringLiteral("[%1:%2]").arg(info.packedRange->left).arg(info.packedRange->right);
        row.binding = bindingSummary(bound[id]);
        rows.push_back(std::move(row));
        inspectorIds_.push_back(id);
    }
    inspector_->resetRows(std::move(rows));
}

bool BoardAdapter::addWatch(const QString& name) {
    if (!canAccessBoard() || !board_) return false;
    const QString watched = name.trimmed();
    if (watched.isEmpty()) return false;
    const auto reject = [this](const QString& message) {
        QScopedValueRollback<bool> publishing(publishing_, true);
        inspector_->setWatchError(message);
        return false;
    };
    if (inspector_->watchCount() >= SignalInspectorModel::MaximumWatches)
        return reject(tr("At most %1 watches can be added.").arg(SignalInspectorModel::MaximumWatches));
    // Plain names never resolve to internal signals, and ports are listed.
    if (!watched.contains(QLatin1Char('.')))
        return reject(tr("Ports are listed automatically. Watch an internal signal by its "
                         "hierarchical name, such as top.signal."));
    for (int row = 0; row < inspector_->count(); ++row) {
        if (inspector_->rows_[static_cast<std::size_t>(row)].name == watched)
            return reject(tr("%1 is already listed.").arg(watched));
    }
    const SignalId id = board_->findSignal(watched.toStdString());
    if (id == kNoSignal) {
        return reject(tr("No readable signal named %1. Names start with the top module; "
                         "memories and signals wider than 64 bits cannot be watched.").arg(watched));
    }
    const SignalInfo info = board_->signalInfo(id);
    SignalInspectorModel::Row row;
    row.name = watched;
    row.kind = SignalInspectorModel::Internal;
    row.width = static_cast<int>(info.width);
    if (info.packedRange)
        row.range = QStringLiteral("[%1:%2]").arg(info.packedRange->left).arg(info.packedRange->right);
    row.watch = true;
    // Read now so the first snapshot does not report a change.
    row.value = board_->readSignal(id);
    {
        QScopedValueRollback<bool> publishing(publishing_, true);
        inspector_->setWatchError({});
        inspectorIds_.push_back(id);
        inspector_->appendWatch(std::move(row));
    }
    return refresh();
}

bool BoardAdapter::removeWatch(const QString& name) {
    if (!canAccessBoard() || !board_) return false;
    for (int row = 0; row < inspector_->count(); ++row) {
        const auto& candidate = inspector_->rows_[static_cast<std::size_t>(row)];
        if (!candidate.watch || candidate.name != name) continue;
        QScopedValueRollback<bool> publishing(publishing_, true);
        inspectorIds_.erase(inspectorIds_.begin() + row);
        inspector_->removeRow(row);
        inspector_->setWatchError({});
        return true;
    }
    return false;
}

std::vector<EventLogModel::Event> BoardAdapter::drainLog() {
    std::vector<EventLogModel::Event> events;
    if (!board_ || !eventLog_->recording()) return events;
    const auto& lines = board_->structuredLog();
    events.reserve(lines.size());
    for (const auto& line : lines) {
        EventLogModel::Event event;
        if (EventLogModel::parseLine(line, event)) events.push_back(std::move(event));
    }
    // The view owns the log while recording: keep the board's copy bounded.
    board_->clearLog();
    return events;
}

bool BoardAdapter::setLogRecording(bool on) {
    if (!canAccessBoard() || !board_) return false;
    if (eventLog_->recording() == on) return true;
    const uint64_t cycle = board_->now();
    std::vector<EventLogModel::Event> events;
    if (on) {
        logWasEnabled_ = board_->logEnabled();
        board_->clearLog();  // earlier lines were not recorded by this view
        board_->setLogEnabled(true);
        events.push_back({EventLogModel::Notice, cycle,
                          tr("Recording started at cycle %1").arg(cycle)});
    } else {
        events = drainLog();
        board_->setLogEnabled(logWasEnabled_);
        events.push_back({EventLogModel::Notice, cycle,
                          tr("Recording stopped at cycle %1").arg(cycle)});
    }
    QScopedValueRollback<bool> publishing(publishing_, true);
    eventLog_->setRecording(on);
    eventLog_->append(std::move(events));
    return true;
}

bool BoardAdapter::clearEventLog() {
    if (!canAccessBoard()) return false;
    QScopedValueRollback<bool> publishing(publishing_, true);
    // Events logged before the click are cleared too, however long ago the
    // last refresh ran; only later events can appear afterwards.
    drainLog();
    eventLog_->clearEvents();
    return true;
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

bool BoardAdapter::sendUartText(const QString& text) {
    if (!canAccessBoard() || !board_) return false;
    if (!board_->hasUartRx())
        return rejectUartSend(tr("This design does not bind the UART receive pin (RsRx, B18)."));
    if (text.isEmpty()) return false;
    // toUtf8() silently drops unpaired surrogates; never send part of a text.
    if (!text.isValidUtf16())
        return rejectUartSend(tr("Not sent: the text contains characters that cannot be encoded as UTF-8."));
    const QByteArray bytes = text.toUtf8();
    if (bytes.size() > UartConsoleModel::MaximumPendingRxBytes) {
        return rejectUartSend(tr("Not sent: this text is %1 B, but at most %2 B can be queued. "
                                 "Send it in shorter parts.")
                                  .arg(bytes.size()).arg(UartConsoleModel::MaximumPendingRxBytes));
    }
    const uint64_t now = board_->now();
    while (!uartRxFrameEnds_.empty() && uartRxFrameEnds_.front() <= now)
        uartRxFrameEnds_.pop_front();
    const auto space = UartConsoleModel::MaximumPendingRxBytes
        - static_cast<qsizetype>(uartRxFrameEnds_.size());
    if (bytes.size() > space) {
        return rejectUartSend(tr("Not sent: needs %1 B of transmit-queue space, but only %2 of "
                                 "%3 B are free. Run or step to transmit queued input.")
                                  .arg(bytes.size()).arg(space)
                                  .arg(UartConsoleModel::MaximumPendingRxBytes));
    }
    std::vector<UartConsoleModel::Byte> sent;
    sent.reserve(static_cast<std::size_t>(bytes.size()));
    for (const char character : bytes) {
        const auto value = static_cast<uint8_t>(character);
        const uint64_t start = board_->sendUart(value);
        sent.push_back({value, start});
        uartRxFrameEnds_.push_back(start + 10 * kUartCyclesPerBit);
    }
    uartRxQueued_ += bytes.size();
    publish(sent);
    return true;
}

bool BoardAdapter::rejectUartSend(const QString& message) {
    QScopedValueRollback<bool> publishing(publishing_, true);
    uart_->setSendError(message);
    return false;
}

bool BoardAdapter::clearUart() {
    if (!canAccessBoard()) return false;
    QScopedValueRollback<bool> publishing(publishing_, true);
    // Traffic decoded before the click is cleared too, however long ago the
    // last refresh ran; only later traffic can appear afterwards.
    const auto update = stageUart();
    uart_->publishCounters(update.countersChanged);
    uart_->clearLines();
    uart_->setSendError({});
    return true;
}

void BoardAdapter::noteReset(uint64_t cycle, uint64_t cycles) {
    if (!canAccessBoard() || !uart_->available()) return;
    QScopedValueRollback<bool> publishing(publishing_, true);
    const auto update = stageUart();
    uart_->publishCounters(update.countersChanged);
    publishUart(update, {}, UartNotice{cycle, tr("Reset: BTNC held for %1 cycles").arg(cycles)});
}

BoardAdapter::UartUpdate BoardAdapter::stageUart() {
    UartUpdate update;
    qint64 txBytes = 0;
    if (board_ && board_->hasUartTx()) {
        const auto& bytes = board_->uartTxBytes();
        const auto& cycles = board_->uartTxByteCycles();
        // The board only appends; clamp anyway so a shorter list cannot underflow.
        uartTxSeen_ = std::min(uartTxSeen_, bytes.size());
        update.tx.reserve(bytes.size() - uartTxSeen_);
        for (std::size_t i = uartTxSeen_; i < bytes.size(); ++i)
            update.tx.push_back({bytes[i], cycles[i]});
        uartTxSeen_ = bytes.size();
        txBytes = static_cast<qint64>(bytes.size());
        const auto& errors = board_->uartTxFramingErrorCycles();
        uartFramingSeen_ = std::min(uartFramingSeen_, errors.size());
        update.framingErrors.assign(errors.begin() + static_cast<std::ptrdiff_t>(uartFramingSeen_),
                                    errors.end());
        uartFramingSeen_ = errors.size();
    }
    if (board_) {
        const uint64_t now = board_->now();
        while (!uartRxFrameEnds_.empty() && uartRxFrameEnds_.front() <= now)
            uartRxFrameEnds_.pop_front();
    }
    update.countersChanged = uart_->stageCounters(
        txBytes, uartRxQueued_, static_cast<qint64>(uartRxFrameEnds_.size()),
        static_cast<qint64>(uartFramingSeen_));
    return update;
}

void BoardAdapter::publishUart(const UartUpdate& update,
                               std::span<const UartConsoleModel::Byte> rx,
                               const std::optional<UartNotice>& reset) {
    // Order by stamp. One grid sample yields a byte or a framing error, never
    // both; a reset notice follows everything stamped at or before its start.
    std::vector<UartNotice> notices;
    for (const uint64_t cycle : update.framingErrors)
        notices.push_back({cycle, tr("Framing error: stop bit sampled low")});
    if (reset) notices.push_back(*reset);
    std::stable_sort(notices.begin(), notices.end(),
                     [](const auto& a, const auto& b) { return a.cycle < b.cycle; });
    std::span<const UartConsoleModel::Byte> tx(update.tx);
    for (const auto& notice : notices) {
        const auto split = std::find_if(tx.begin(), tx.end(), [&](const auto& byte) {
            return byte.cycle > notice.cycle;
        });
        const auto before = static_cast<std::size_t>(split - tx.begin());
        uart_->appendTraffic(UartConsoleModel::Tx, tx.first(before));
        uart_->appendNotice(notice.text, notice.cycle);
        tx = tx.subspan(before);
    }
    uart_->appendTraffic(UartConsoleModel::Tx, tx);
    if (!rx.empty()) {
        // Each send starts a row stamped with its own first start bit.
        uart_->endLine();
        uart_->appendTraffic(UartConsoleModel::Rx, rx);
    }
}

bool BoardAdapter::refresh() {
    if (!canAccessBoard()) return false;
    publish({});
    return true;
}

void BoardAdapter::publish(std::span<const UartConsoleModel::Byte> rx) {
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

    const auto uartUpdate = stageUart();

    // The monitor's framebuffer is copied once per completed frame, never per
    // refresh; the copy is independent of later board frames.
    VgaFrameModel::State vgaState;
    QImage vgaImage;
    if (board_ && board_->hasVga()) {
        vgaState.completedFrames = static_cast<qint64>(board_->vgaCompletedFrames());
        vgaState.frameCycle = board_->vgaLastFrameCycle();
        vgaState.cyclesPerPixel = static_cast<int>(board_->vgaCyclesPerPixel());
        vgaState.ok = board_->vgaOk();
        vgaState.status = QString::fromStdString(board_->vgaStatus());
        const auto& rgb = board_->vgaFramebuffer();
        constexpr auto rowBytes = VgaFrameModel::Width * 3;
        if (vgaState.completedFrames != vga_->completedFrames()
            && rgb.size() == static_cast<std::size_t>(rowBytes) * VgaFrameModel::Height) {
            vgaImage = QImage(rgb.data(), VgaFrameModel::Width, VgaFrameModel::Height,
                              rowBytes, QImage::Format_RGB888).copy();
        }
    }
    const bool vgaChanged = vga_->stage(vgaState, std::move(vgaImage));

    // One snapshot of every inspected signal, all read at the same cycle.
    SignalInspectorModel::Changes inspectorChanges;
    std::vector<EventLogModel::Event> logEvents;
    if (board_) {
        std::vector<uint64_t> values;
        values.reserve(inspectorIds_.size());
        for (const SignalId id : inspectorIds_) values.push_back(board_->readSignal(id));
        inspectorChanges = inspector_->stageValues(values, board_->now());
        logEvents = drainLog();
    }

    const auto switchChanges = switches_->stageStates(switches);
    const auto ledChanges = leds_->stageStates(leds);
    const auto buttonChanges = buttons_->stageStates(buttons);
    const auto digitChanges = digits_->stageStates(digits);
    // A successful send supersedes an earlier rejection.
    if (!rx.empty()) uart_->setSendError({});
    switches_->publishChanges(switchChanges);
    leds_->publishChanges(ledChanges);
    buttons_->publishChanges(buttonChanges);
    digits_->publishChanges(digitChanges);
    vga_->publish(vgaChanged);
    inspector_->publishValues(inspectorChanges);
    uart_->publishCounters(uartUpdate.countersChanged);
    // List rows must be inserted during notification, so they publish last.
    publishUart(uartUpdate, rx);
    eventLog_->append(std::move(logEvents));
}

}  // namespace vb::qt
