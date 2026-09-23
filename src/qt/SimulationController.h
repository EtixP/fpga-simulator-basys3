#pragma once

#include "qt/BoardAdapter.h"

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <functional>

class QThread;
class QTimer;

namespace vb { class BoardModel; }

namespace vb::qt {

// GUI-thread scheduling over BoardModel. Wall time chooses when a fixed batch
// runs, never how many cycles the batch advances. The composition root owns
// engine -> board -> adapter -> controller -> QML, in that lifetime order.
class SimulationController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")
    Q_PROPERTY(vb::qt::BoardAdapter* board READ board CONSTANT FINAL)
    Q_PROPERTY(QString designName READ designName CONSTANT FINAL)
    Q_PROPERTY(bool connected READ connected CONSTANT FINAL)
    Q_PROPERTY(bool canReset READ canReset CONSTANT FINAL)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool realtime READ realtime WRITE setRealtime NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString cycleText READ cycleText NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString virtualTimeText READ virtualTimeText NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool speedAvailable READ speedAvailable NOTIFY stateChanged FINAL)
    Q_PROPERTY(double cyclesPerSecond READ cyclesPerSecond NOTIFY stateChanged FINAL)
    Q_PROPERTY(double realtimeMultiplier READ realtimeMultiplier NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString errorString READ errorString NOTIFY stateChanged FINAL)

public:
    static constexpr uint64_t BatchCycles = 100'000;
    static constexpr uint32_t MaximumStepCycles = 1'000'000;
    static constexpr uint64_t ResetCycles = 16;

    // Clock/timer injection is a C++ test seam, never available to QML. The
    // monotonic clock returns nanoseconds. Production uses steady_clock.
    struct Options {
        uint64_t batchCycles = BatchCycles;
        bool automaticScheduling = true;
        std::function<qint64()> nowNanoseconds;
    };

    explicit SimulationController(BoardAdapter& adapter, QString designName,
                                  QObject* parent = nullptr);
    SimulationController(BoardAdapter& adapter, QString designName, Options options,
                         QObject* parent = nullptr);

    BoardAdapter* board() const { return adapter_; }
    QString designName() const { return designName_; }
    bool connected() const { return board_ != nullptr; }
    bool canReset() const { return canReset_; }
    bool running() const { return running_; }
    bool realtime() const { return realtime_; }
    QString cycleText() const { return cycleText_; }
    QString virtualTimeText() const { return virtualTimeText_; }
    bool speedAvailable() const { return speedAvailable_; }
    double cyclesPerSecond() const { return cyclesPerSecond_; }
    double realtimeMultiplier() const { return cyclesPerSecond_ / 100'000'000.0; }
    QString errorString() const { return errorString_; }

    Q_INVOKABLE bool run();
    Q_INVOKABLE bool pause();
    Q_INVOKABLE bool step(uint32_t cycles = 1);
    Q_INVOKABLE bool reset();
    void setRealtime(bool enabled);

    // One event-loop turn, also used by deterministic C++ pacing tests. A stale
    // queued callback after Pause is a no-op. This is not invokable from QML.
    void processBatch();

signals:
    void stateChanged();

private:
    bool canMutate() const;
    bool advance(uint64_t cycles);
    void publish(qint64 now);
    void clearMeasurement(qint64 now);
    void fail(const QString& message);
    int pacingDelay(qint64 now) const;
    void schedule(int milliseconds);

    BoardModel* const board_;
    BoardAdapter* const adapter_;
    QThread* const ownerThread_;
    const QString designName_;
    const bool canReset_;
    const Options options_;
    QTimer* const timer_;
    bool busy_ = false;
    bool running_ = false;
    bool realtime_ = false;
    bool speedAvailable_ = false;
    double cyclesPerSecond_ = 0;
    QString cycleText_;
    QString virtualTimeText_;
    QString errorString_;
    uint64_t runCycle_ = 0;
    qint64 runWall_ = 0;
    uint64_t sampleCycle_ = 0;
    qint64 sampleWall_ = 0;
    qint64 lastPublishWall_ = 0;
};

}  // namespace vb::qt
