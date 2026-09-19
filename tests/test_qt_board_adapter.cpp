// The Qt seam is tested against independently supplied board pins. The
// expected state comes from the stimulus, not another adapter or renderer.
#include "board/BoardModel.h"
#include "check.h"
#include "qt/BoardAdapter.h"
#include "qt/BoardIoModel.h"
#include "qt/SevenSegmentModel.h"

#include <QAbstractItemModelTester>
#include <QCoreApplication>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QtQml/qqmlextensionplugin.h>

#include <array>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

Q_IMPORT_QML_PLUGIN(VirtualBasys_BoardPlugin)

using namespace vb;
using namespace vb::qt;

namespace {
class PinEngine final : public SimEngine {
public:
  enum Port : uint32_t { Switch, Buttons, Rx, Led, Anode, Segment, Dp };
  std::array<uint64_t, 7> values{0, 0, 0, 0, 15, 127, 1};
  unsigned steps = 0;
  unsigned pokes = 0;
  unsigned peeks = 0;
  std::vector<std::pair<uint64_t, uint64_t>> rxPokes;

  SignalId lookup(std::string_view name) override {
    for (size_t i = 0; i < metadata_.size(); ++i)
      if (metadata_[i].name == name) return SignalId(i);
    return kNoSignal;
  }
  SignalInfo info(SignalId id) const override { return metadata_.at(size_t(id)); }
  std::vector<SignalInfo> ports() const override { return metadata_; }
  void step(uint64_t cycles) override { ++steps; now_ += cycles; }
  uint64_t now() const override { return now_; }
  uint64_t peek(SignalId id) override {
    ++peeks;
    const size_t index = size_t(id);
    if (index >= values.size()) throw std::invalid_argument("invalid signal");
    // A combinational output proves an input publication refreshes the whole
    // board without requiring a clock edge or a BoardModel::tick(0).
    if (index == Led) return values[Led] ^ values[Switch];
    return values[index];
  }
  void poke(SignalId id, uint64_t value) override {
    const size_t index = size_t(id);
    if (index > Rx) throw std::invalid_argument("not an input");
    ++pokes;
    values[index] = value & ((uint64_t{1} << metadata_[index].width) - 1);
    if (index == Rx) rxPokes.emplace_back(now_, values[index]);
  }
  void setTraceFile(std::string_view) override { CHECK(false); }
  void trace(bool) override { CHECK(false); }

private:
  uint64_t now_ = 0;
  const std::vector<SignalInfo> metadata_{
      {"inputs", 2, true}, {"keys", 2, true}, {"serial", 1, true},
      {"lamps", 2, false}, {"an", 4, false}, {"seg", 7, false},
      {"dp", 1, false}};
};

// The board resources deliberately do not match the HDL port names, and
// SW0/LED0/BTNC use packed bit 1. GUI row indices must never become port bits.
constexpr const char* kXdc = R"(
set_property PACKAGE_PIN V17 [get_ports {inputs[1]}]
set_property PACKAGE_PIN R2 [get_ports {inputs[0]}]
set_property PACKAGE_PIN U18 [get_ports {keys[1]}]
set_property PACKAGE_PIN U17 [get_ports {keys[0]}]
set_property PACKAGE_PIN B18 [get_ports serial]
set_property PACKAGE_PIN U16 [get_ports {lamps[1]}]
set_property PACKAGE_PIN L1 [get_ports {lamps[0]}]
set_property PACKAGE_PIN U2 [get_ports {an[0]}]
set_property PACKAGE_PIN U4 [get_ports {an[1]}]
set_property PACKAGE_PIN V4 [get_ports {an[2]}]
set_property PACKAGE_PIN W4 [get_ports {an[3]}]
set_property PACKAGE_PIN W7 [get_ports {seg[0]}]
set_property PACKAGE_PIN W6 [get_ports {seg[1]}]
set_property PACKAGE_PIN U8 [get_ports {seg[2]}]
set_property PACKAGE_PIN V8 [get_ports {seg[3]}]
set_property PACKAGE_PIN U5 [get_ports {seg[4]}]
set_property PACKAGE_PIN V5 [get_ports {seg[5]}]
set_property PACKAGE_PIN U7 [get_ports {seg[6]}]
set_property PACKAGE_PIN V7 [get_ports dp]
)";

PinBinding binding(PinEngine& engine) {
  auto result = PinBinding::bind(parseXdc(kXdc), engine);
  CHECK(result.diagnostics().empty());
  return result;
}

struct Fixture {
  PinEngine engine;
  BoardModel board{engine, binding(engine)};
  BoardAdapter adapter{&board};
  Fixture() { board.setLogEnabled(true); }
};

QVariant value(const QAbstractItemModel* model, int row, int role) {
  return model->data(model->index(row, 0), role);
}

bool active(const BoardIoModel* model, int row) {
  return value(model, row, BoardIoModel::ActiveRole).toBool();
}

using Changes = std::map<int, std::set<int>>;

void expectChanges(QSignalSpy& spy, const Changes& expected) {
  Changes actual;
  for (const auto& arguments : spy) {
    CHECK_EQ(arguments.size(), 3);
    const QModelIndex first = qvariant_cast<QModelIndex>(arguments[0]);
    const QModelIndex last = qvariant_cast<QModelIndex>(arguments[1]);
    const auto roles = qvariant_cast<QList<int>>(arguments[2]);
    CHECK(first.isValid() && last.isValid());
    CHECK(first.model() == last.model());
    CHECK_EQ(first.column(), 0);
    CHECK_EQ(last.column(), 0);
    CHECK(first.row() <= last.row());
    CHECK(!roles.empty());
    for (int row = first.row(); row <= last.row(); ++row)
      for (int role : roles) CHECK(actual[row].insert(role).second);
  }
  CHECK(actual == expected);
  spy.clear();
}

void modelShape(BoardAdapter& adapter) {
  const std::array<QAbstractItemModel*, 4> models{
      adapter.switches(), adapter.leds(), adapter.buttons(), adapter.digits()};
  constexpr std::array<int, 4> sizes{16, 16, 5, 4};
  for (size_t i = 0; i < models.size(); ++i) {
    auto* model = models[i];
    QAbstractItemModelTester tester(
        model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    CHECK_EQ(model->rowCount(), sizes[i]);
    CHECK_EQ(model->columnCount(), 1);
    CHECK_EQ(model->rowCount(model->index(0, 0)), 0);
    CHECK(!model->index(-1, 0).isValid());
    CHECK(!model->index(sizes[i], 0).isValid());
    CHECK(!model->index(0, -1).isValid());
    CHECK(!model->index(0, 1).isValid());
    CHECK(!model->index(0, 0, model->index(0, 0)).isValid());
    CHECK(!model->data(QModelIndex(), Qt::DisplayRole).isValid());
    CHECK(!model->data(models[(i + 1) % models.size()]->index(0, 0),
                       Qt::DisplayRole).isValid());
    CHECK(!model->data(model->index(0, 0), -100).isValid());
    for (int row = 0; row < sizes[i]; ++row) {
      CHECK(!(model->flags(model->index(row, 0)) & Qt::ItemIsEditable));
      CHECK(!model->setData(model->index(row, 0), true, Qt::EditRole));
    }
  }
  const auto ioRoles = adapter.switches()->roleNames();
  CHECK(ioRoles.value(BoardIoModel::ResourceRole) == "resource");
  CHECK(ioRoles.value(BoardIoModel::AvailableRole) == "available");
  CHECK(ioRoles.value(BoardIoModel::ActiveRole) == "active");
  CHECK(adapter.leds()->roleNames() == ioRoles);
  CHECK(adapter.buttons()->roleNames() == ioRoles);
  const auto digitRoles = adapter.digits()->roleNames();
  CHECK(digitRoles.value(SevenSegmentModel::DigitRole) == "digit");
  CHECK(digitRoles.value(SevenSegmentModel::SegmentsRole) == "segments");
  CHECK(digitRoles.value(SevenSegmentModel::DecimalPointRole) == "decimalPoint");
  CHECK(digitRoles.value(SevenSegmentModel::CharacterRole) == "character");
}

void disconnectedAndAvailability() {
  BoardAdapter empty;
  CHECK(!empty.connected());
  CHECK(!empty.hasDisplay());
  modelShape(empty);
  CHECK(empty.refresh());
  for (auto* model : {empty.switches(), empty.leds(), empty.buttons()}) {
    for (int row = 0; row < model->rowCount(); ++row) {
      CHECK(!value(model, row, BoardIoModel::AvailableRole).toBool());
      CHECK(!active(model, row));
    }
  }
  CHECK(!empty.setSwitch(0, true));
  CHECK(!empty.setButton(0, true));
  for (int digit = 0; digit < 4; ++digit) {
    CHECK_EQ(value(empty.digits(), digit, SevenSegmentModel::DigitRole).toInt(), digit);
    CHECK_EQ(value(empty.digits(), digit, SevenSegmentModel::SegmentsRole).toInt(), 0);
    CHECK(!value(empty.digits(), digit, SevenSegmentModel::DecimalPointRole).toBool());
    CHECK(value(empty.digits(), digit, SevenSegmentModel::CharacterRole).toString() == " ");
    CHECK(value(empty.digits(), digit, Qt::DisplayRole).toString() == " ");
  }

  Fixture f;
  CHECK(f.adapter.connected());
  CHECK(f.adapter.hasDisplay());
  modelShape(f.adapter);
  for (int row = 0; row < 16; ++row) {
    for (auto* model : {f.adapter.switches(), f.adapter.leds()})
      CHECK_EQ(value(model, row, BoardIoModel::AvailableRole).toBool(),
               row == 0 || row == 15);
    CHECK(value(f.adapter.switches(), row, BoardIoModel::ResourceRole).toString() ==
          QString("SW%1").arg(row));
    CHECK(value(f.adapter.leds(), row, Qt::DisplayRole).toString() ==
          QString("LED%1").arg(row));
  }
  for (int row = 0; row < 5; ++row) {
    CHECK(value(f.adapter.buttons(), row, BoardIoModel::ResourceRole).toString() ==
          QString::fromLatin1(kButtonNames[size_t(row)]));
    CHECK_EQ(value(f.adapter.buttons(), row, BoardIoModel::AvailableRole).toBool(),
             row == 0 || row == 4);
  }
  PinEngine bareEngine;
  BoardModel bareBoard(bareEngine, PinBinding{});
  BoardAdapter bare(&bareBoard);
  CHECK(bare.connected());
  CHECK(!bare.hasDisplay());
  CHECK(!bare.setSwitch(0, true));
  CHECK(!bare.setButton(0, true));

  PinEngine initialEngine;
  BoardModel initialBoard(initialEngine, binding(initialEngine));
  initialBoard.setSwitch(15, true);
  initialBoard.setButton(Button::C, true);
  BoardAdapter initial(&initialBoard);
  CHECK(active(initial.switches(), 15));
  CHECK(active(initial.leds(), 15));
  CHECK(active(initial.buttons(), 0));
  CHECK_EQ(initialEngine.steps, 0);
}

void inputsSnapshotsAndNoTimeAdvance() {
  Fixture f;
  auto& a = f.adapter;
  QSignalSpy switches(a.switches(), &QAbstractItemModel::dataChanged);
  QSignalSpy leds(a.leds(), &QAbstractItemModel::dataChanged);
  QSignalSpy buttons(a.buttons(), &QAbstractItemModel::dataChanged);
  QSignalSpy digits(a.digits(), &QAbstractItemModel::dataChanged);
  CHECK(switches.isValid() && leds.isValid() && buttons.isValid() && digits.isValid());
  const auto originalLog = f.board.structuredLog();
  const auto originalPokes = f.engine.pokes;
  for (int row : {-1, 1, 14, 16, 999}) CHECK(!a.setSwitch(row, true));
  for (int row : {-1, 1, 3, 5, 999}) CHECK(!a.setButton(row, true));
  CHECK_EQ(f.engine.pokes, originalPokes);
  CHECK(f.board.structuredLog() == originalLog);
  CHECK(a.setSwitch(0, true));
  CHECK_EQ(f.engine.values[PinEngine::Switch], 2);
  CHECK(active(a.switches(), 0));
  CHECK(active(a.leds(), 0));
  CHECK(!active(a.leds(), 15));
  expectChanges(switches, {{0, {BoardIoModel::ActiveRole}}});
  expectChanges(leds, {{0, {BoardIoModel::ActiveRole}}});
  CHECK(a.setButton(4, true));
  CHECK_EQ(f.engine.values[PinEngine::Buttons], 1);
  CHECK(f.board.buttonState(Button::D));
  expectChanges(buttons, {{4, {BoardIoModel::ActiveRole}}});
  const auto changedLog = f.board.structuredLog();
  CHECK_EQ(changedLog.size(), originalLog.size() + 2);
  CHECK(changedLog[originalLog.size()] == "[cycle 0] SW0 0->1");
  CHECK(changedLog.back() == "[cycle 0] BTND 0->1");
  const auto changedPokes = f.engine.pokes;
  CHECK(a.setSwitch(0, true));
  CHECK(a.setButton(4, true));
  CHECK(a.refresh());
  CHECK_EQ(f.engine.pokes, changedPokes);
  CHECK(f.board.structuredLog() == changedLog);
  expectChanges(switches, {});
  expectChanges(leds, {});
  expectChanges(buttons, {});
  expectChanges(digits, {});

  // An external C++ owner controls time and can change inputs. Models retain
  // one published snapshot until the owner explicitly calls refresh().
  f.board.setSwitch(15, true);
  CHECK(!active(a.switches(), 15));
  CHECK(!active(a.leds(), 15));
  const auto readsBeforeData = f.engine.peeks;
  modelShape(a);
  CHECK_EQ(f.engine.peeks, readsBeforeData);
  CHECK(a.refresh());
  CHECK(active(a.switches(), 15) && active(a.leds(), 15));
  expectChanges(switches, {{15, {BoardIoModel::ActiveRole}}});
  expectChanges(leds, {{15, {BoardIoModel::ActiveRole}}});

  f.board.setSwitch(0, false);
  f.board.setSwitch(15, false);
  CHECK(a.refresh());
  expectChanges(switches, {{0, {BoardIoModel::ActiveRole}}, {15, {BoardIoModel::ActiveRole}}});
  expectChanges(leds, {{0, {BoardIoModel::ActiveRole}}, {15, {BoardIoModel::ActiveRole}}});

  // Calling tick(0) would consume this queued start bit despite advancing no
  // cycles. Refresh, input setters, and Qt event dispatch must leave it queued.
  f.board.sendUart(0x96);
  const auto rxPokes = f.engine.rxPokes;
  const auto beforeRefreshLog = f.board.structuredLog();
  CHECK(a.refresh());
  CHECK(a.setSwitch(0, false));
  QCoreApplication::processEvents();
  CHECK_EQ(f.engine.now(), 0);
  CHECK_EQ(f.engine.steps, 0);
  CHECK_EQ(f.engine.values[PinEngine::Rx], 1);
  CHECK(f.engine.rxPokes == rxPokes);
  CHECK(f.board.structuredLog() == beforeRefreshLog);
}

void fusedDisplayAndVirtualPersistence() {
  Fixture f;
  auto* model = f.adapter.digits();
  QSignalSpy changes(model, &QAbstractItemModel::dataChanged);
  constexpr std::array<int, 4> segments{0x3F, 0x06, 0x5B, 0x4F};
  for (int digit = 0; digit < 4; ++digit) {
    f.engine.values[PinEngine::Anode] = 15 ^ (1 << digit);
    f.engine.values[PinEngine::Segment] = 0x7F ^ segments[size_t(digit)];
    f.engine.values[PinEngine::Dp] = digit % 2;
    f.board.tick(1000);
  }
  CHECK_EQ(value(model, 0, SevenSegmentModel::SegmentsRole).toInt(), 0);
  CHECK(f.adapter.refresh());
  Changes expected;
  for (int digit = 0; digit < 4; ++digit) {
    CHECK_EQ(value(model, digit, SevenSegmentModel::SegmentsRole).toInt(), segments[size_t(digit)]);
    CHECK_EQ(value(model, digit, SevenSegmentModel::DecimalPointRole).toBool(), digit % 2 == 0);
    CHECK(value(model, digit, SevenSegmentModel::CharacterRole).toString() == QString::number(digit));
    CHECK(value(model, digit, Qt::DisplayRole).toString() == QString::number(digit));
    expected[digit] = {SevenSegmentModel::SegmentsRole, SevenSegmentModel::CharacterRole,
                       Qt::DisplayRole};
    if (digit % 2 == 0) expected[digit].insert(SevenSegmentModel::DecimalPointRole);
  }
  expectChanges(changes, expected);
  f.engine.values[PinEngine::Anode] = 15;
  f.engine.values[PinEngine::Segment] = 127;
  f.engine.values[PinEngine::Dp] = 1;
  // The exact persistence boundary is inclusive and measured in virtual
  // cycles. Raw all-dark cathodes must not overwrite the fused snapshot.
  f.board.tick(1000 + SevenSeg::kPersistCycles - f.board.now());
  CHECK(f.adapter.refresh());
  expectChanges(changes, {});
  CHECK_EQ(value(model, 0, SevenSegmentModel::SegmentsRole).toInt(), 0x3F);
  f.board.tick(1);
  CHECK(f.adapter.refresh());
  expectChanges(changes, {{0, {SevenSegmentModel::SegmentsRole,
                              SevenSegmentModel::DecimalPointRole,
                              SevenSegmentModel::CharacterRole, Qt::DisplayRole}}});
  CHECK_EQ(value(model, 0, SevenSegmentModel::SegmentsRole).toInt(), 0);
  CHECK_EQ(value(model, 1, SevenSegmentModel::SegmentsRole).toInt(), 0x06);
}

void displayRoleGranularity() {
  Fixture f;
  auto* model = f.adapter.digits();
  QSignalSpy changes(model, &QAbstractItemModel::dataChanged);
  f.engine.values[PinEngine::Anode] = 14;
  f.engine.values[PinEngine::Segment] = 0x7E;  // Lit mask 1: undecodable.
  f.board.tick(1000);
  CHECK(f.adapter.refresh());
  CHECK(value(model, 0, SevenSegmentModel::CharacterRole).toString() == "?");
  expectChanges(changes, {{0, {SevenSegmentModel::SegmentsRole,
                              SevenSegmentModel::CharacterRole, Qt::DisplayRole}}});
  f.engine.values[PinEngine::Segment] = 0x7D;  // Lit mask 2: still undecodable.
  f.board.tick(1000);
  CHECK(f.adapter.refresh());
  expectChanges(changes, {{0, {SevenSegmentModel::SegmentsRole}}});
  f.engine.values[PinEngine::Dp] = 0;
  f.board.tick(1000);
  CHECK(f.adapter.refresh());
  expectChanges(changes, {{0, {SevenSegmentModel::DecimalPointRole}}});
  f.board.tick(1000);  // A new anode sample alone changes no presentation role.
  CHECK(f.adapter.refresh());
  expectChanges(changes, {});
}

void coherentPublicationAndThreadGuard() {
  Fixture f;
  auto& a = f.adapter;
  f.board.setSwitch(0, true);
  f.board.setButton(Button::C, true);
  f.engine.values[PinEngine::Anode] = 14;
  f.engine.values[PinEngine::Segment] = 0x79;
  f.board.tick(1000);
  const auto expectedLog = f.board.structuredLog();
  const auto expectedPokes = f.engine.pokes;
  int callbacks = 0;
  const auto connection = QObject::connect(a.switches(), &QAbstractItemModel::dataChanged,
      &a, [&](const QModelIndex&, const QModelIndex&, const QList<int>&) {
        ++callbacks;
        CHECK(active(a.switches(), 0));
        CHECK(active(a.leds(), 0));
        CHECK(active(a.buttons(), 0));
        CHECK(value(a.digits(), 0, SevenSegmentModel::CharacterRole).toString() == "1");
        CHECK(!a.setSwitch(15, true));
        CHECK(!a.setButton(4, true));
        CHECK(!a.refresh());
      });
  CHECK(a.refresh());
  CHECK_EQ(callbacks, 1);
  QObject::disconnect(connection);
  CHECK_EQ(f.engine.pokes, expectedPokes);
  CHECK(f.board.structuredLog() == expectedLog);
  CHECK(!f.board.switchState(15));
  CHECK(!f.board.buttonState(Button::D));
  const auto expectedPeeks = f.engine.peeks;
  std::thread wrongThread([&] {
    CHECK(!a.setSwitch(15, true));
    CHECK(!a.setButton(4, true));
    CHECK(!a.refresh());
  });
  wrongThread.join();
  CHECK_EQ(f.engine.pokes, expectedPokes);
  CHECK_EQ(f.engine.peeks, expectedPeeks);
  CHECK_EQ(f.board.now(), 1000);
  CHECK(f.board.structuredLog() == expectedLog);
}

void typedQmlBoundary() {
  Fixture f;
  const auto* meta = f.adapter.metaObject();
  for (const char* property : {"connected", "hasDisplay", "switches", "leds", "buttons", "digits"}) {
    const int index = meta->indexOfProperty(property);
    CHECK(index >= 0);
    CHECK(meta->property(index).isConstant());
    CHECK(!meta->property(index).isWritable());
  }
  for (const char* forbidden : {"board", "engine", "binding", "now"})
    CHECK_EQ(meta->indexOfProperty(forbidden), -1);
  for (int method = meta->methodOffset(); method < meta->methodCount(); ++method)
    for (const char* forbidden : {"refresh", "tick", "step", "stepCapture", "peek", "poke",
                                  "trace", "setTraceFile", "sendUart", "binding", "board", "engine"})
      CHECK(meta->method(method).name() != forbidden);

  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQml
import VirtualBasys.Board
QtObject {
    required property BoardAdapter board
    readonly property BoardIoModel switchRows: board.switches
    readonly property BoardIoModel ledRows: board.leds
    readonly property BoardIoModel buttonRows: board.buttons
    readonly property SevenSegmentModel digitRows: board.digits
    readonly property bool connected: board.connected
    readonly property bool displayPresent: board.hasDisplay
    function drive() { return board.setSwitch(15, true) && board.setButton(4, true) }
    function reject() { return !board.setSwitch(-1, true) && !board.setButton(5, true) }
    function isolated() {
        return typeof board.refresh === "undefined" && typeof board.tick === "undefined"
            && typeof board.engine === "undefined" && typeof board.peek === "undefined"
    }
}
)", QUrl("qrc:/adapter-test.qml"));
  if (component.isError()) qWarning().noquote() << component.errorString();
  CHECK(component.isReady());
  const std::unique_ptr<QObject> object(component.createWithInitialProperties(
      {{"board", QVariant::fromValue(&f.adapter)}}));
  if (!object) qWarning().noquote() << component.errorString();
  CHECK(object != nullptr);
  CHECK(object->property("connected").toBool());
  CHECK(object->property("displayPresent").toBool());
  CHECK(object->property("switchRows").value<BoardIoModel*>() == f.adapter.switches());
  CHECK(object->property("ledRows").value<BoardIoModel*>() == f.adapter.leds());
  CHECK(object->property("buttonRows").value<BoardIoModel*>() == f.adapter.buttons());
  CHECK(object->property("digitRows").value<SevenSegmentModel*>() == f.adapter.digits());
  for (const char* method : {"drive", "reject", "isolated"}) {
    QVariant result;
    CHECK(QMetaObject::invokeMethod(object.get(), method, Q_RETURN_ARG(QVariant, result)));
    CHECK(result.toBool());
  }
  CHECK(f.board.switchState(15));
  CHECK(f.board.buttonState(Button::D));
  CHECK_EQ(f.engine.values[PinEngine::Switch], 1);
  CHECK_EQ(f.engine.values[PinEngine::Buttons], 1);
  CHECK_EQ(f.board.now(), 0);
  CHECK_EQ(f.engine.steps, 0);
  QQmlComponent uncreatable(&engine);
  uncreatable.setData("import VirtualBasys.Board\nBoardAdapter {}", QUrl("qrc:/invalid-adapter.qml"));
  CHECK(uncreatable.isError());
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  disconnectedAndAvailability();
  inputsSnapshotsAndNoTimeAdvance();
  fusedDisplayAndVirtualPersistence();
  displayRoleGranularity();
  coherentPublicationAndThreadGuard();
  typedQmlBoundary();
  std::puts("test_qt_board_adapter: PASS");
}
