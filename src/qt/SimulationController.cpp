#include "qt/SimulationController.h"

#include "board/BoardModel.h"
#include "qt/VirtualTime.h"

#include <QScopedValueRollback>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <utility>

namespace vb::qt {
namespace {
constexpr qint64 PublishInterval = 16'000'000;
constexpr qint64 MeasurementInterval = 250'000'000;
// BoardModel's next observation grid must remain representable. Stop at the
// last complete grid rather than letting its next-grid arithmetic wrap.
constexpr uint64_t MaximumCycle = std::numeric_limits<uint64_t>::max()
    - std::numeric_limits<uint64_t>::max() % BoardModel::kSampleChunkCycles;

SimulationController::Options validated(SimulationController::Options options) {
    if (!options.batchCycles || options.batchCycles > SimulationController::MaximumStepCycles)
        throw std::invalid_argument("simulation batch must contain 1..1000000 cycles");
    if (!options.nowNanoseconds) {
        options.nowNanoseconds = [] {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        };
    }
    return options;
}
}  // namespace

SimulationController::SimulationController(BoardAdapter& adapter, QString designName,
                                           QObject* parent)
    : SimulationController(adapter, std::move(designName), Options{}, parent) {}

SimulationController::SimulationController(BoardAdapter& adapter, QString designName,
                                           Options options, QObject* parent)
    : QObject(parent), board_(adapter.board_), adapter_(&adapter),
      ownerThread_(QThread::currentThread()), designName_(std::move(designName)),
      canReset_(board_ && board_->hasButton(Button::C)),
      options_(validated(std::move(options))), timer_(new QTimer(this)) {
    if (!adapter.canAccessBoard())
        throw std::invalid_argument("controller and adapter must share an idle owner thread");
    timer_->setSingleShot(true);
    timer_->setTimerType(Qt::PreciseTimer);
    connect(timer_, &QTimer::timeout, this, &SimulationController::processBatch);
    QScopedValueRollback<bool> operation(busy_, true);
    const auto now = options_.nowNanoseconds();
    clearMeasurement(now);
    publish(now);
}

bool SimulationController::canMutate() const {
    return QThread::currentThread() == ownerThread_ && thread() == ownerThread_
        && !busy_ && board_ && adapter_->canAccessBoard();
}

void SimulationController::clearMeasurement(qint64 now) {
    speedAvailable_ = false;
    cyclesPerSecond_ = 0;
    runCycle_ = sampleCycle_ = board_ ? board_->now() : 0;
    runWall_ = sampleWall_ = lastPublishWall_ = now;
}

void SimulationController::publish(qint64 now) {
    const uint64_t cycle = board_ ? board_->now() : 0;
    cycleText_ = QString::number(cycle);
    virtualTimeText_ = formatVirtualTime(cycle);
    if (running_ && now >= sampleWall_ && now - sampleWall_ >= MeasurementInterval) {
        cyclesPerSecond_ = static_cast<double>(cycle - sampleCycle_) * 1e9
            / static_cast<double>(now - sampleWall_);
        speedAvailable_ = true;
        sampleCycle_ = cycle;
        sampleWall_ = now;
    }
    lastPublishWall_ = now;
    adapter_->refresh();
    emit stateChanged();
    if (finished_ && !finishAnnounced_) {
        finishAnnounced_ = true;
        emit runFinished();
    }
}

void SimulationController::fail(const QString& message) {
    running_ = false;
    timer_->stop();
    errorString_ = message;
    clearMeasurement(options_.nowNanoseconds());
}

ScriptSend SimulationController::scriptSend() {
    // Scripted sends appear in the UART terminal like typed ones.
    return [this](std::string_view text) { adapter_->sendScriptedUart(text); };
}

void SimulationController::finish() {
    running_ = false;
    timer_->stop();
    finished_ = true;
}

bool SimulationController::advance(uint64_t cycles) {
    // A finite scripted run never passes its last cycle.
    if (script_ && script_->endCycle)
        cycles = std::min(cycles, *script_->endCycle - board_->now());
    if (board_->now() > MaximumCycle || cycles > MaximumCycle - board_->now()) {
        fail(QStringLiteral("The virtual cycle counter has reached its limit."));
        return false;
    }
    try {
        // One time-advance path: script events apply at their exact cycles
        // whether Run, Step or Reset advances time.
        if (script_)
            advanceScripted(*board_, script_->options, script_->cursor, cycles, scriptSend());
        else
            board_->tick(cycles);
        if (script_ && script_->endCycle && board_->now() == *script_->endCycle) finish();
        return true;
    } catch (const std::exception& error) {
        fail(QString::fromUtf8(error.what()));
        return false;
    }
}

bool SimulationController::startScript(RunOptions options) {
    if (!canMutate() || script_ || running_ || !errorString_.isEmpty() || board_->now() != 0
        || !options.cyclesPerFrame) return false;
    QScopedValueRollback<bool> operation(busy_, true);
    script_.emplace(Script{std::move(options), {}, std::nullopt});
    try {
        initializeScriptedRun(*board_, script_->options, script_->cursor, scriptSend());
        const auto& run = script_->options;
        if (run.maxFrames >= 0) {
            // Startup took 16 cycles for a positive run and none for zero frames.
            const auto frames = static_cast<uint64_t>(run.maxFrames);
            if (frames > (MaximumCycle - board_->now()) / run.cyclesPerFrame)
                throw std::overflow_error("the scripted run exceeds the virtual cycle range");
            script_->endCycle = board_->now() + frames * run.cyclesPerFrame;
            endCycleText_ = QString::number(*script_->endCycle);
            if (board_->now() == *script_->endCycle) finish();
        }
    } catch (const std::exception& error) {
        // A script that could not start never runs, not even after a Reset.
        fail(QString::fromUtf8(error.what()));
        scriptFailed_ = true;
    }
    const auto now = options_.nowNanoseconds();
    clearMeasurement(now);
    publish(now);
    return errorString_.isEmpty();
}

std::pair<std::size_t, std::size_t> SimulationController::unappliedScriptEvents() const {
    if (!script_) return {0, 0};
    return {script_->options.stimulus.size() - script_->cursor.event,
            script_->options.sends.size() - script_->cursor.send};
}

bool SimulationController::run() {
    if (!canMutate() || !errorString_.isEmpty() || finished_ || scriptFailed_) return false;
    if (running_) return true;
    QScopedValueRollback<bool> operation(busy_, true);
    running_ = true;
    const auto now = options_.nowNanoseconds();
    clearMeasurement(now);
    publish(now);
    schedule(0);
    return true;
}

bool SimulationController::pause() {
    if (!canMutate()) return false;
    QScopedValueRollback<bool> operation(busy_, true);
    running_ = false;
    timer_->stop();
    const auto now = options_.nowNanoseconds();
    clearMeasurement(now);
    publish(now);
    return true;
}

bool SimulationController::step(uint32_t cycles) {
    if (!canMutate() || running_ || !cycles || cycles > MaximumStepCycles
        || !errorString_.isEmpty() || finished_ || scriptFailed_) return false;
    QScopedValueRollback<bool> operation(busy_, true);
    const bool success = advance(cycles);
    publish(options_.nowNanoseconds());
    return success;
}

bool SimulationController::reset() {
    if (!canMutate() || !canReset_ || finished_ || scriptFailed_) return false;
    QScopedValueRollback<bool> operation(busy_, true);
    running_ = false;
    timer_->stop();
    if (board_->now() > MaximumCycle || ResetCycles > MaximumCycle - board_->now()) {
        fail(QStringLiteral("Not enough virtual cycles remain for a reset pulse."));
        publish(options_.nowNanoseconds());
        return false;
    }
    errorString_.clear();
    const bool wasPressed = board_->buttonState(Button::C);
    const uint64_t pulseStart = board_->now();
    const std::size_t eventsBefore = script_ ? script_->cursor.event : 0;
    bool success = false;
    try {
        board_->setButton(Button::C, true);
        success = advance(ResetCycles);
        // A reset command does not take ownership of an existing physical hold,
        // and a scripted BTNC event during the pulse owns BTNC afterwards, as
        // during a scripted run's startup reset.
        const bool scriptedButton = script_ && std::any_of(
            script_->options.stimulus.begin() + static_cast<std::ptrdiff_t>(eventsBefore),
            script_->options.stimulus.begin() + static_cast<std::ptrdiff_t>(script_->cursor.event),
            [](const StimulusEvent& event) { return event.name == "BTNC"; });
        if (!scriptedButton) board_->setButton(Button::C, wasPressed);
    } catch (const std::exception& error) {
        fail(QString::fromUtf8(error.what()));
    }
    // Presentation only: mark a completed pulse among UART terminal traffic. A
    // finite scripted run may end during the pulse.
    if (success && errorString_.isEmpty())
        adapter_->noteReset(pulseStart, board_->now() - pulseStart);
    const auto now = options_.nowNanoseconds();
    clearMeasurement(now);
    publish(now);
    return success && errorString_.isEmpty();
}

void SimulationController::setRealtime(bool enabled) {
    if (!canMutate() || realtime_ == enabled) return;
    QScopedValueRollback<bool> operation(busy_, true);
    realtime_ = enabled;
    const auto now = options_.nowNanoseconds();
    // Switching modes starts a new pacing/measurement window without resetting
    // virtual time, altering inputs or repaying a previous mode's wall-time lag.
    clearMeasurement(now);
    publish(now);
    if (running_) schedule(0);
}

int SimulationController::pacingDelay(qint64 now) const {
    if (!realtime_) return 0;
    const auto elapsed = now >= runWall_ ? static_cast<uint64_t>(now - runWall_) : 0;
    const auto elapsedCycles = elapsed / 10;
    const auto advanced = board_->now() - runCycle_;
    if (advanced <= elapsedCycles) return 0;
    const auto ahead = advanced - elapsedCycles;
    // 100,000 cycles = 1 ms. Round waits up, without multiplying uint64 cycles.
    const auto milliseconds = ahead / 100'000 + (ahead % 100'000 != 0);
    return static_cast<int>(std::min<uint64_t>(milliseconds, 1000));
}

void SimulationController::schedule(int milliseconds) {
    // A continuously rearmed zero timer can starve native Qt Quick updates.
    // Yield at least 1 ms between batches in both modes; this changes only wall
    // throughput, never the fixed cycle budget or peripheral observations.
    if (running_ && options_.automaticScheduling) timer_->start(std::max(1, milliseconds));
}

void SimulationController::processBatch() {
    if (QThread::currentThread() != ownerThread_ || thread() != ownerThread_ || !running_)
        return;
    if (!canMutate()) {
        // A model listener can run a nested event loop while the adapter is
        // publishing. Consuming this single-shot callback must not silently
        // strand Run with no timer. Retry after the outer operation unwinds.
        schedule(1);
        return;
    }
    QScopedValueRollback<bool> operation(busy_, true);
    const auto before = options_.nowNanoseconds();
    if (const int delay = pacingDelay(before); delay > 0) {
        if (before - lastPublishWall_ >= PublishInterval) publish(before);
        schedule(delay);
        return;
    }
    const bool success = advance(options_.batchCycles);
    const auto after = options_.nowNanoseconds();
    // The last batch of a finite run always publishes its final state.
    if (!success || finished_ || after - lastPublishWall_ >= PublishInterval) publish(after);
    schedule(pacingDelay(after));
}

}  // namespace vb::qt
