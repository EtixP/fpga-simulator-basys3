#include "qt/SimulationController.h"

#include "board/BoardModel.h"

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
    // Divide first: cycle*10 would overflow long before the cycle counter does.
    virtualTimeText_ = QStringLiteral("%1.%2 s")
        .arg(cycle / 100'000'000)
        .arg((cycle % 100'000'000) * 10, 9, 10, QLatin1Char('0'));
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
}

void SimulationController::fail(const QString& message) {
    running_ = false;
    timer_->stop();
    errorString_ = message;
    clearMeasurement(options_.nowNanoseconds());
}

bool SimulationController::advance(uint64_t cycles) {
    if (board_->now() > MaximumCycle || cycles > MaximumCycle - board_->now()) {
        fail(QStringLiteral("The virtual cycle counter has reached its limit."));
        return false;
    }
    try {
        board_->tick(cycles);
        return true;
    } catch (const std::exception& error) {
        fail(QString::fromUtf8(error.what()));
        return false;
    }
}

bool SimulationController::run() {
    if (!canMutate() || !errorString_.isEmpty()) return false;
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
        || !errorString_.isEmpty()) return false;
    QScopedValueRollback<bool> operation(busy_, true);
    const bool success = advance(cycles);
    publish(options_.nowNanoseconds());
    return success;
}

bool SimulationController::reset() {
    if (!canMutate() || !canReset_) return false;
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
    bool success = false;
    try {
        board_->setButton(Button::C, true);
        success = advance(ResetCycles);
        // A reset command does not take ownership of an existing physical hold.
        board_->setButton(Button::C, wasPressed);
    } catch (const std::exception& error) {
        fail(QString::fromUtf8(error.what()));
    }
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
    if (!success || after - lastPublishWall_ >= PublishInterval) publish(after);
    schedule(pacingDelay(after));
}

}  // namespace vb::qt
