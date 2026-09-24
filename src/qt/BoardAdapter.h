#pragma once

#include "qt/BoardIoModel.h"
#include "qt/SevenSegmentModel.h"
#include "qt/UartConsoleModel.h"

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

public:
    explicit BoardAdapter(BoardModel* board = nullptr, QObject* parent = nullptr);

    bool connected() const { return board_ != nullptr; }
    bool hasDisplay() const { return hasDisplay_; }
    BoardIoModel* switches() const { return switches_; }
    BoardIoModel* leds() const { return leds_; }
    BoardIoModel* buttons() const { return buttons_; }
    SevenSegmentModel* digits() const { return digits_; }
    UartConsoleModel* uart() const { return uart_; }

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
    std::size_t uartTxSeen_ = 0;
    std::size_t uartFramingSeen_ = 0;
    qint64 uartRxQueued_ = 0;
    std::deque<uint64_t> uartRxFrameEnds_;  // queued frames still on the line
    bool publishing_ = false;
};

}  // namespace vb::qt
