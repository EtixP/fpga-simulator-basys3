#pragma once

#include "qt/BoardIoModel.h"
#include "qt/SevenSegmentModel.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

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

public:
    explicit BoardAdapter(BoardModel* board = nullptr, QObject* parent = nullptr);

    bool connected() const { return board_ != nullptr; }
    bool hasDisplay() const { return hasDisplay_; }
    BoardIoModel* switches() const { return switches_; }
    BoardIoModel* leds() const { return leds_; }
    BoardIoModel* buttons() const { return buttons_; }
    SevenSegmentModel* digits() const { return digits_; }

    // True means accepted, including an idempotent write. Invalid/unbound
    // resources, calls on another thread or during notification return false.
    Q_INVOKABLE bool setSwitch(int index, bool on);
    Q_INVOKABLE bool setButton(int index, bool pressed);

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

    BoardModel* const board_;
    QThread* const ownerThread_;
    const bool hasDisplay_;
    BoardIoModel* const switches_;
    BoardIoModel* const leds_;
    BoardIoModel* const buttons_;
    SevenSegmentModel* const digits_;
    bool publishing_ = false;
};

}  // namespace vb::qt
