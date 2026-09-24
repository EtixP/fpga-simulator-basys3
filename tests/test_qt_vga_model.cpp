// VGA presentation seam: BoardModel's monitor -> BoardAdapter -> VgaFrameModel.
// A synthetic design drives exact 800x525 raster timing at one cycle per
// pixel with a frame-dependent pattern, so expected images and stamps come
// from raster arithmetic here, not from the monitor or the adapter.
#include "board/BoardModel.h"
#include "check.h"
#include "qt_vga_raster.h"
#include "qt/BoardAdapter.h"
#include "qt/VgaDisplay.h"
#include "qt/VgaFrameModel.h"

#include <QCoreApplication>
#include <QDebug>
#include <QMetaMethod>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QtQml/qqmlextensionplugin.h>

#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

Q_IMPORT_QML_PLUGIN(VirtualBasys_BoardPlugin)

using namespace vb;
using namespace vb::qt;

using namespace vga_raster;

namespace {
struct Fixture {
  explicit Fixture(bool allPins = true)
      : board(engine, PinBinding::bind(parseXdc(xdc(allPins)), engine)) {}
  RasterEngine engine;
  BoardModel board;
  BoardAdapter adapter{&board};
  VgaFrameModel& vga() { return *adapter.vga(); }
  void runTo(uint64_t cycle) {
    CHECK(cycle >= board.now());
    board.tick(cycle - board.now());
  }
};

void availability() {
  BoardAdapter empty;
  CHECK(empty.vga() != nullptr && empty.vga()->parent() == &empty);
  CHECK(!empty.vga()->available() && !empty.vga()->hasFrame());
  CHECK(empty.vga()->image().isNull());
  CHECK(empty.refresh());

  Fixture partial(false);
  CHECK(!partial.board.hasVga());
  CHECK(!partial.vga().available());
  partial.runTo(2 * kFrame);
  CHECK(partial.adapter.refresh());
  CHECK(!partial.vga().hasFrame() && partial.vga().image().isNull());

  Fixture full;
  CHECK(full.vga().available());
  CHECK_EQ(full.vga().width(), 640);
  CHECK_EQ(full.vga().height(), 480);
}

void framesStampsAndCopies() {
  Fixture f;
  QSignalSpy changed(f.adapter.vga(), &VgaFrameModel::frameChanged);
  CHECK(!f.vga().hasFrame());
  CHECK(f.vga().frameCycleText().isEmpty() && f.vga().frameTimeText().isEmpty());
  CHECK_EQ(f.vga().imageSerial(), 0);

  // The frame completes exactly when the post-edge state of its closing
  // Vsync fall is observed: stamp + 1.
  f.runTo(frameStamp(0));
  CHECK(f.adapter.refresh());
  CHECK(!f.vga().hasFrame());
  CHECK_EQ(f.vga().cyclesPerPixel(), 1);  // measured from Hsync before any frame
  const int beforeFrame = int(changed.size());
  f.runTo(frameStamp(0) + 1);
  CHECK(f.adapter.refresh());
  CHECK_EQ(int(changed.size()), beforeFrame + 1);
  CHECK(f.vga().hasFrame());
  CHECK_EQ(f.vga().completedFrames(), 1);
  CHECK(f.vga().frameCycleText() == QString::number(frameStamp(0)));
  CHECK(f.vga().frameTimeText() == QStringLiteral("0.00%1 s")
        .arg(frameStamp(0) * 10, 7, 10, QLatin1Char('0')));
  CHECK(f.vga().ok() && f.vga().status().isEmpty());
  CHECK_EQ(f.vga().imageSerial(), 1);
  // The image mirrors the board framebuffer and the independent raster.
  const auto& board = f.board.vgaFramebuffer();
  CHECK(imageEquals(f.vga().image(), board));
  CHECK(imageEquals(f.vga().image(), expectedFrame(0)));

  // Refreshes without a new frame publish nothing and never copy.
  const QImage first = f.vga().image();
  const auto afterFrame = changed.size();
  CHECK(f.adapter.refresh());
  f.runTo(f.board.now() + kFrame / 3);
  CHECK(f.adapter.refresh());
  CHECK_EQ(changed.size(), afterFrame);
  CHECK_EQ(f.vga().imageSerial(), 1);
  CHECK(f.vga().image().constBits() == first.constBits());  // same shared copy

  // A new frame replaces the image; the earlier copy is unaffected.
  f.runTo(frameStamp(1) + 1);
  CHECK(f.adapter.refresh());
  CHECK_EQ(f.vga().completedFrames(), 2);
  CHECK_EQ(f.vga().imageSerial(), 2);
  CHECK(imageEquals(f.vga().image(), expectedFrame(1)));
  CHECK(imageEquals(first, expectedFrame(0)));
  CHECK(!imageEquals(first, expectedFrame(1)));

  // Several frames between refreshes: only the latest is copied and shown.
  f.runTo(frameStamp(4) + 1);
  CHECK(f.adapter.refresh());
  CHECK_EQ(f.vga().completedFrames(), 5);
  CHECK_EQ(f.vga().imageSerial(), 3);
  CHECK(f.vga().frameCycleText() == QString::number(frameStamp(4)));
  CHECK(imageEquals(f.vga().image(), expectedFrame(4)));
  CHECK(imageEquals(f.vga().image(), f.board.vgaFramebuffer()));
}

void statusFollowsTheLatestFrame() {
  Fixture f;
  f.runTo(frameStamp(0) + 1);
  CHECK(f.adapter.refresh());
  CHECK(f.vga().ok());
  // Active-high Hsync throughout the next period: the monitor flags it.
  f.engine.invertHsync = true;
  f.runTo(frameStamp(2) + 1);
  CHECK(f.adapter.refresh());
  CHECK(!f.vga().ok());
  CHECK(f.vga().status().contains(QStringLiteral("active-HIGH")));
  CHECK(f.vga().status() == QString::fromStdString(f.board.vgaStatus()));
  // Like a real monitor, a later clean frame clears the diagnosis.
  f.engine.invertHsync = false;
  QSignalSpy changed(f.adapter.vga(), &VgaFrameModel::frameChanged);
  f.runTo(frameStamp(5) + 1);
  CHECK(f.adapter.refresh());
  CHECK(f.vga().ok() && f.vga().status().isEmpty());
  CHECK_EQ(changed.size(), 1);
  CHECK(imageEquals(f.vga().image(), expectedFrame(5)));
}

void stagedBeforeNotification() {
  // Every model is staged before any is notified: a frame listener sees the
  // new button state, and a button listener sees the new frame.
  Fixture f;
  f.runTo(frameStamp(0) + 1);
  CHECK(f.adapter.refresh());
  f.runTo(frameStamp(1) + 1);
  f.board.setButton(Button::C, true);  // the adapter's button row is now stale
  auto* buttons = f.adapter.buttons();
  bool buttonSeenByFrameListener = false;
  qint64 framesSeenByButtonListener = 0;
  QObject::connect(f.adapter.vga(), &VgaFrameModel::frameChanged, f.adapter.vga(), [&] {
    buttonSeenByFrameListener =
        buttons->data(buttons->index(0, 0), BoardIoModel::ActiveRole).toBool();
  });
  QObject::connect(buttons, &QAbstractItemModel::dataChanged, buttons,
                   [&] { framesSeenByButtonListener = f.vga().completedFrames(); });
  CHECK(f.adapter.refresh());
  CHECK(buttonSeenByFrameListener);
  CHECK_EQ(framesSeenByButtonListener, 2);
}

void refreshIsPassiveAndGuarded() {
  Fixture f;
  f.runTo(frameStamp(0) + 1);
  const auto steps = f.engine.steps;
  const auto now = f.board.now();
  CHECK(f.adapter.refresh());
  CHECK(f.adapter.refresh());
  CHECK_EQ(f.engine.steps, steps);
  CHECK_EQ(f.board.now(), now);

  // Listeners cannot re-enter the adapter while the frame is published.
  int reentered = 0;
  QObject::connect(f.adapter.vga(), &VgaFrameModel::frameChanged, f.adapter.vga(), [&] {
    reentered += f.adapter.refresh() + f.adapter.setButton(0, true) + f.adapter.clearUart();
  });
  f.runTo(frameStamp(1) + 1);
  CHECK(f.adapter.refresh());
  CHECK_EQ(reentered, 0);
  CHECK(!f.board.buttonState(Button::C));

  // Another thread cannot refresh (and so cannot copy a frame).
  bool accepted = true;
  std::thread worker([&] { accepted = f.adapter.refresh(); });
  worker.join();
  CHECK(!accepted);
}

void qmlExposure() {
  // The model is read-only for QML: no invokables, only its change signal.
  const QMetaObject& meta = VgaFrameModel::staticMetaObject;
  for (int i = meta.methodOffset(); i < meta.methodCount(); ++i)
    CHECK(meta.method(i).methodType() == QMetaMethod::Signal);
  for (const char* hidden : {"stage", "publish", "image", "imageSerial"})
    CHECK_EQ(meta.indexOfMethod(hidden), -1);
  CHECK(VgaDisplay::staticMetaObject.indexOfProperty("frame") >= 0);

  Fixture f;
  f.runTo(frameStamp(0) + 1);
  CHECK(f.adapter.refresh());
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
    import QtQml
    import VirtualBasys.Board
    QtObject {
      required property BoardAdapter board
      readonly property VgaFrameModel vga: board.vga
      property bool available: vga.available
      property bool hasFrame: vga.hasFrame
      property int frames: vga.completedFrames
      property string cycle: vga.frameCycleText
      property int cpp: vga.cyclesPerPixel
      property bool ok: vga.ok
    })", QUrl());
  std::unique_ptr<QObject> object(component.createWithInitialProperties(
      {{QStringLiteral("board"), QVariant::fromValue(&f.adapter)}}));
  if (!object) qWarning() << component.errors();
  CHECK(object != nullptr);
  CHECK(object->property("available").toBool() && object->property("hasFrame").toBool());
  CHECK_EQ(object->property("frames").toInt(), 1);
  CHECK(object->property("cycle").toString() == QString::number(frameStamp(0)));
  CHECK_EQ(object->property("cpp").toInt(), 1);
  CHECK(object->property("ok").toBool());
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  availability();
  framesStampsAndCopies();
  statusFollowsTheLatestFrame();
  stagedBeforeNotification();
  refreshIsPassiveAndGuarded();
  qmlExposure();
  std::puts("test_qt_vga_model: PASS");
  return 0;
}
