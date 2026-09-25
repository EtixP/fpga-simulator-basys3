#pragma once

#include "qt/BoardIoModel.h"
#include "qt/EventLogModel.h"
#include "qt/SevenSegmentModel.h"
#include "qt/SignalInspectorModel.h"
#include "qt/UartConsoleModel.h"
#include "qt/VgaFrameModel.h"

#include "engine/SimEngine.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

class QThread;

namespace vb { class BoardModel; }

namespace vb::qt {

// Borrowed board, fixed for this adapter's lifetime. The composition root owns
// engine -> board -> adapter -> QML engine, destroying them in reverse order.
// All operations stay on the construction/GUI thread; do not move this object
// or access the board concurrently. Child models own presentation snapshots.
class BoardAdapter : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")
    Q_PROPERTY(bool connected READ connected CONSTANT FINAL)
    Q_PROPERTY(bool hasDisplay READ hasDisplay CONSTANT FINAL)
    Q_PROPERTY(vb::qt::BoardIoModel* switches READ switches CONSTANT FINAL)
    Q_PROPERTY(vb::qt::BoardIoModel* leds READ leds CONSTANT FINAL)
    Q_PROPERTY(vb::qt::BoardIoModel* buttons READ buttons CONSTANT FINAL)
    Q_PROPERTY(vb::qt::SevenSegmentModel* digits READ digits CONSTANT FINAL)
    Q_PROPERTY(vb::qt::UartConsoleModel* uart READ uart CONSTANT FINAL)
    Q_PROPERTY(vb::qt::VgaFrameModel* vga READ vga CONSTANT FINAL)
    Q_PROPERTY(vb::qt::SignalInspectorModel* inspector READ inspector CONSTANT FINAL)
    Q_PROPERTY(vb::qt::SignalFilterModel* inspectorView READ inspectorView CONSTANT FINAL)
    Q_PROPERTY(vb::qt::EventLogModel* eventLog READ eventLog CONSTANT FINAL)
    Q_PROPERTY(vb::qt::EventFilterModel* eventLogView READ eventLogView CONSTANT FINAL)

public:
    explicit BoardAdapter(BoardModel* board = nullptr, QObject* parent = nullptr);
    // Restores the board's log enable state if the log view is still recording.
    ~BoardAdapter() override;

    bool connected() const { return board_ != nullptr; }
    bool hasDisplay() const { return hasDisplay_; }
    BoardIoModel* switches() const { return switches_; }
    BoardIoModel* leds() const { return leds_; }
    BoardIoModel* buttons() const { return buttons_; }
    SevenSegmentModel* digits() const { return digits_; }
    UartConsoleModel* uart() const { return uart_; }
    VgaFrameModel* vga() const { return vga_; }
    SignalInspectorModel* inspector() const { return inspector_; }
    SignalFilterModel* inspectorView() const { return inspectorView_; }
    EventLogModel* eventLog() const { return eventLog_; }
    EventFilterModel* eventLogView() const { return eventLogView_; }

    // True means accepted, including an idempotent write. Invalid/unbound
    // resources, calls on another thread or during notification return false.
    Q_INVOKABLE bool setSwitch(int index, bool on);
    Q_INVOKABLE bool setButton(int index, bool pressed);

    // Queues the text's UTF-8 bytes, unchanged and without an added line
    // ending, to the design's receive line. The board schedules each start
    // bit; advancing no time. All or none are queued: empty text, an unbound
    // receive pin or insufficient pending-queue space return false (the last
    // two explain why in uart.sendError), as do wrong-thread/reentrant calls.
    Q_INVOKABLE bool sendUartText(const QString& text);
    // Empties the terminal scrollback and any send error only. Board bytes,
    // queued input, logs and virtual time are unchanged; later traffic starts
    // a new row.
    Q_INVOKABLE bool clearUart();

    // Adds a read-only inspector row for a hierarchical RTL name rooted at the
    // top module ("counter.count"); ports are always listed. Returns false,
    // with inspector.watchError explaining, for unknown or unsupported names,
    // duplicates or more than SignalInspectorModel::MaximumWatches watches.
    Q_INVOKABLE bool addWatch(const QString& name);
    Q_INVOKABLE bool removeWatch(const QString& name);

    // While recording, the log view owns BoardModel's structured log: it
    // clears earlier lines, enables collection, and drains then clears new
    // lines on every refresh, so the board's log memory stays bounded.
    // Stopping drains the remaining lines and restores the previous enable
    // state. Recording and its notices never advance time.
    Q_INVOKABLE bool setLogRecording(bool on);
    // Empties the log view only; recording continues.
    Q_INVOKABLE bool clearEventLog();

    // C++ controller seam, deliberately absent from QML's metaobject. Reads
    // BoardModel only; never tick(), even tick(0). LED reads may settle pending
    // inputs via the board's normal peek semantics, without advancing time.
    // Stage every model before notifying; reject reentrant writes/refreshes.
    bool refresh();

private:
    // The scheduling peer must reject stepping during synchronous model
    // publication; neither this seam nor the borrowed board is exposed to QML.
    friend class SimulationController;
    bool canAccessBoard() const;
    // Controller seam after a completed reset pulse that began at `cycle`:
    // publish unshown UART traffic with a notice ordered after everything
    // stamped at or before `cycle`, so the order never depends on refreshes.
    void noteReset(uint64_t cycle, uint64_t cycles);

    struct UartUpdate {
        bool countersChanged = false;
        std::vector<UartConsoleModel::Byte> tx;
        std::vector<uint64_t> framingErrors;  // stamps, ascending
    };
    struct UartNotice {
        uint64_t cycle = 0;
        QString text;
    };
    void buildInspector();
    std::vector<EventLogModel::Event> drainLog();
    // Reads the board only; publication follows every other model's staging.
    UartUpdate stageUart();
    void publishUart(const UartUpdate& update, std::span<const UartConsoleModel::Byte> rx,
                     const std::optional<UartNotice>& reset = std::nullopt);
    void publish(std::span<const UartConsoleModel::Byte> rx);
    bool rejectUartSend(const QString& message);

    BoardModel* const board_;
    QThread* const ownerThread_;
    const bool hasDisplay_;
    BoardIoModel* const switches_;
    BoardIoModel* const leds_;
    BoardIoModel* const buttons_;
    SevenSegmentModel* const digits_;
    UartConsoleModel* const uart_;
    VgaFrameModel* const vga_;
    SignalInspectorModel* const inspector_;
    SignalFilterModel* const inspectorView_;
    EventLogModel* const eventLog_;
    EventFilterModel* const eventLogView_;
    std::vector<SignalId> inspectorIds_;  // parallel to inspector rows; C++ only
    bool logWasEnabled_ = false;
    std::size_t uartTxSeen_ = 0;
    std::size_t uartFramingSeen_ = 0;
    qint64 uartRxQueued_ = 0;
    std::deque<uint64_t> uartRxFrameEnds_;  // queued frames still on the line
    bool publishing_ = false;
};

}  // namespace vb::qt
