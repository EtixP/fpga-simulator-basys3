// Inspector and event-log seams: BoardModel reads -> BoardAdapter -> batched
// Qt models. A synthetic design computes its values arithmetically, and the
// expected log is a second board's own structured log under the same stimulus.
#include "board/BoardModel.h"
#include "check.h"
#include "qt/BoardAdapter.h"
#include "qt/EventLogModel.h"
#include "qt/SignalInspectorModel.h"

#include <QAbstractItemModelTester>
#include <QCoreApplication>
#include <QDebug>
#include <QMetaMethod>
#include <QSignalSpy>
#include <QtQml/qqmlextensionplugin.h>

#include <cstdlib>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

Q_IMPORT_QML_PLUGIN(VirtualBasys_BoardPlugin)

using namespace vb;
using namespace vb::qt;

namespace {
// Ports (sorted by name, as SimEngine::ports() promises): btn, clk, led,
// spare, sw. Internal: probe.count (16-bit), probe.flag (1-bit).
class ProbeEngine final : public SimEngine {
public:
  enum Id : uint32_t { Btn, Clk, Led, Spare, Sw, Count, Flag };
  static constexpr uint32_t kBitBase = 100;
  uint64_t count = 0;
  unsigned steps = 0;
  unsigned peeks = 0;

  SignalId lookup(std::string_view name) override {
    for (size_t i = 0; i < metadata_.size(); ++i)
      if (metadata_[i].name == name) return SignalId(i);
    if (name == "probe.count") return SignalId(Count);
    if (name == "probe.flag") return SignalId(Flag);
    // probe.bit0 .. probe.bit63: one bit of the counter each.
    if (name.rfind("probe.bit", 0) == 0) {
      const int bit = std::atoi(std::string(name.substr(9)).c_str());
      if (bit >= 0 && bit < 64 && name.size() > 9) return SignalId(kBitBase + bit);
    }
    return kNoSignal;
  }
  SignalInfo info(SignalId id) const override {
    if (size_t(id) == Count) return {"probe.count", 16, false};
    if (size_t(id) == Flag) return {"probe.flag", 1, false};
    if (uint32_t(id) >= kBitBase)
      return {"probe.bit" + std::to_string(uint32_t(id) - kBitBase), 1, false};
    return metadata_.at(size_t(id));
  }
  std::vector<SignalInfo> ports() const override { return metadata_; }
  // Each rising edge adds the switch value to the counter.
  void step(uint64_t cycles) override {
    ++steps;
    count = (count + cycles * sw_) & 0xFFFF;
    now_ += cycles;
  }
  uint64_t now() const override { return now_; }
  uint64_t peek(SignalId id) override {
    ++peeks;
    if (uint32_t(id) >= kBitBase) return (count >> (uint32_t(id) - kBitBase)) & 1;
    switch (size_t(id)) {
      case Btn: return btn_;
      case Clk: return 1;
      case Led: return count;
      case Spare: return 0xA5;
      case Sw: return sw_;
      case Count: return count;
      case Flag: return count & 1;
      default: throw std::invalid_argument("invalid signal");
    }
  }
  void poke(SignalId id, uint64_t value) override {
    if (size_t(id) == Sw) sw_ = value & 15;
    else if (size_t(id) == Btn) btn_ = value & 1;
    else throw std::invalid_argument("not an input");
  }
  void setTraceFile(std::string_view) override { CHECK(false); }
  void trace(bool) override { CHECK(false); }

private:
  uint64_t now_ = 0;
  uint64_t sw_ = 0;
  uint64_t btn_ = 0;
  const std::vector<SignalInfo> metadata_{
      {"btn", 1, true}, {"clk", 1, true}, {"led", 16, false},
      {"spare", 8, false}, {"sw", 4, true}};
};

std::string probeXdc() {
  std::string xdc =
      "set_property PACKAGE_PIN W5 [get_ports clk]\n"
      "set_property PACKAGE_PIN U18 [get_ports btn]\n";
  const char* sw[] = {"V17", "V16", "W16", "W17"};
  for (int bit = 0; bit < 4; ++bit)
    xdc += std::string("set_property PACKAGE_PIN ") + sw[bit] + " [get_ports {sw[" +
           std::to_string(bit) + "]}]\n";
  const char* led[] = {"U16", "E19", "U19", "V19", "W18", "U15", "U14", "V14",
                       "V13", "V3", "W3", "U3", "P3", "N3", "P1", "L1"};
  for (int bit = 0; bit < 16; ++bit)
    xdc += std::string("set_property PACKAGE_PIN ") + led[bit] + " [get_ports {led[" +
           std::to_string(bit) + "]}]\n";
  return xdc;  // "spare" stays unconstrained
}

struct Fixture {
  Fixture() : board(engine, PinBinding::bind(parseXdc(probeXdc()), engine)) {}
  ProbeEngine engine;
  BoardModel board;
  BoardAdapter adapter{&board};
  QAbstractItemModelTester inspectorTester{adapter.inspector(),
      QAbstractItemModelTester::FailureReportingMode::Fatal};
  QAbstractItemModelTester logTester{adapter.eventLog(),
      QAbstractItemModelTester::FailureReportingMode::Fatal};
  QAbstractItemModelTester inspectorViewTester{adapter.inspectorView(),
      QAbstractItemModelTester::FailureReportingMode::Fatal};
  QAbstractItemModelTester logViewTester{adapter.eventLogView(),
      QAbstractItemModelTester::FailureReportingMode::Fatal};
  SignalInspectorModel& inspector() { return *adapter.inspector(); }
  EventLogModel& log() { return *adapter.eventLog(); }
};

QVariant cell(const QAbstractItemModel& model, int row, int role) {
  return model.data(model.index(row, 0), role);
}

int rowOf(const QAbstractItemModel& model, const QString& name) {
  for (int row = 0; row < model.rowCount(); ++row)
    if (cell(model, row, SignalInspectorModel::NameRole).toString() == name) return row;
  return -1;
}

void portsKindsBindingsAndValues() {
  Fixture f;
  auto& m = f.inspector();
  CHECK(m.available());
  CHECK_EQ(m.count(), 5);
  const QStringList names{"btn", "clk", "led", "spare", "sw"};
  const std::vector<int> kinds{SignalInspectorModel::Input, SignalInspectorModel::Clock,
                               SignalInspectorModel::Output, SignalInspectorModel::Output,
                               SignalInspectorModel::Input};
  const QStringList ranges{"", "", "[15:0]", "[7:0]", "[3:0]"};
  const QStringList bindings{"BTNC", "CLK100", "LED0–LED15", "", "SW0–SW3"};
  for (int row = 0; row < 5; ++row) {
    CHECK(cell(m, row, SignalInspectorModel::NameRole).toString() == names[row]);
    CHECK_EQ(cell(m, row, SignalInspectorModel::KindRole).toInt(), kinds[size_t(row)]);
    CHECK(cell(m, row, SignalInspectorModel::RangeRole).toString() == ranges[row]);
    CHECK(cell(m, row, SignalInspectorModel::BindingRole).toString() == bindings[row]);
    CHECK(!cell(m, row, SignalInspectorModel::WatchRole).toBool());
    CHECK(!cell(m, row, SignalInspectorModel::ChangedRole).toBool());
  }
  CHECK(cell(m, 3, SignalInspectorModel::ValueRole).toString() == QStringLiteral("0xA5"));
  CHECK(cell(m, 3, SignalInspectorModel::DecimalRole).toString() == QStringLiteral("165"));
  CHECK(cell(m, 1, SignalInspectorModel::ValueRole).toString() == QStringLiteral("1"));
  CHECK(cell(m, 2, SignalInspectorModel::ValueRole).toString() == QStringLiteral("0x0000"));
  CHECK(m.snapshotCycleText() == QStringLiteral("0"));
  const auto roles = m.roleNames();
  CHECK(roles.value(SignalInspectorModel::WidthRole) == "bits");  // never shadows Item.width
  CHECK_EQ(cell(m, 2, SignalInspectorModel::WidthRole).toInt(), 16);
  // Value formatting, independent of the model's rows.
  CHECK(SignalInspectorModel::valueText(5, 3) == QStringLiteral("0x5"));
  CHECK(SignalInspectorModel::valueText(~0ull, 64) == QStringLiteral("0xFFFFFFFFFFFFFFFF"));
}

// Pins collapse into a range only for one stem numbered like consecutive bits.
void pinSummariesNeverMislead() {
  ProbeEngine engine;
  const std::string xdc =
      "set_property PACKAGE_PIN W5 [get_ports clk]\n"
      // Bits in order, switches out of order.
      "set_property PACKAGE_PIN V17 [get_ports {sw[0]}]\n"     // SW0
      "set_property PACKAGE_PIN W16 [get_ports {sw[1]}]\n"     // SW2
      "set_property PACKAGE_PIN V16 [get_ports {sw[2]}]\n"     // SW1
      "set_property PACKAGE_PIN W17 [get_ports {sw[3]}]\n"     // SW3
      // Consecutive LEDs on bits with a gap.
      "set_property PACKAGE_PIN U16 [get_ports {led[0]}]\n"    // LED0
      "set_property PACKAGE_PIN E19 [get_ports {led[2]}]\n"    // LED1
      // Reversed.
      "set_property PACKAGE_PIN L1 [get_ports {spare[0]}]\n"   // LED15
      "set_property PACKAGE_PIN P1 [get_ports {spare[1]}]\n"   // LED14
      "set_property PACKAGE_PIN N3 [get_ports {spare[2]}]\n";  // LED13
  BoardModel board(engine, PinBinding::bind(parseXdc(xdc), engine));
  BoardAdapter adapter(&board);
  const auto& m = *adapter.inspector();
  const auto binding = [&](const char* name) {
    return cell(m, rowOf(m, QString::fromLatin1(name)), SignalInspectorModel::BindingRole).toString();
  };
  CHECK(binding("sw") == QStringLiteral("SW0, SW2, SW1, SW3"));
  CHECK(binding("led") == QStringLiteral("LED0, LED1"));
  CHECK(binding("spare") == QStringLiteral("LED15, LED14, LED13"));
  CHECK(binding("btn").isEmpty());
}

void batchedChangesAtOneCycle() {
  Fixture f;
  auto& m = f.inspector();
  QSignalSpy changed(&m, &QAbstractItemModel::dataChanged);
  QSignalSpy snapshot(&m, &SignalInspectorModel::snapshotChanged);
  // An input write changes exactly one row, reported as one range.
  CHECK(f.adapter.setSwitch(0, true));
  CHECK_EQ(changed.size(), 1);
  CHECK(qvariant_cast<QModelIndex>(changed[0][0]).row() == 4);
  CHECK(qvariant_cast<QModelIndex>(changed[0][1]).row() == 4);
  CHECK(cell(m, 4, SignalInspectorModel::ValueRole).toString() == QStringLiteral("0x1"));
  CHECK(cell(m, 4, SignalInspectorModel::ChangedRole).toBool());
  CHECK_EQ(snapshot.size(), 0);  // same cycle
  changed.clear();
  // Seven cycles: led moves; sw's changed flag clears. Two separate ranges.
  f.board.tick(7);
  CHECK(f.adapter.refresh());
  CHECK_EQ(changed.size(), 2);
  CHECK(qvariant_cast<QModelIndex>(changed[0][0]).row() == 2);
  CHECK(qvariant_cast<QModelIndex>(changed[1][0]).row() == 4);
  CHECK(cell(m, 2, SignalInspectorModel::ValueRole).toString() == QStringLiteral("0x0007"));
  CHECK(cell(m, 2, SignalInspectorModel::ChangedRole).toBool());
  CHECK(!cell(m, 4, SignalInspectorModel::ChangedRole).toBool());
  CHECK(m.snapshotCycleText() == QStringLiteral("7"));
  CHECK_EQ(snapshot.size(), 1);
  for (const auto& arguments : changed) {
    const auto roles = qvariant_cast<QList<int>>(arguments[2]);
    CHECK((roles == QList<int>{SignalInspectorModel::ValueRole, SignalInspectorModel::DecimalRole,
                               SignalInspectorModel::ChangedRole}));
  }
  changed.clear();
  // Same values at a later cycle clear flags only; an idle refresh emits nothing.
  f.adapter.setSwitch(0, false);
  changed.clear();
  f.board.tick(3);
  CHECK(f.adapter.refresh());
  CHECK(!cell(m, 2, SignalInspectorModel::ChangedRole).toBool());
  changed.clear();
  CHECK(f.adapter.refresh());
  CHECK(f.adapter.refresh());
  CHECK_EQ(changed.size(), 0);
  // Adjacent changes merge into one range: watch two internal signals.
  CHECK(f.adapter.addWatch(QStringLiteral("probe.count")));
  CHECK(f.adapter.addWatch(QStringLiteral("probe.flag")));
  CHECK(f.adapter.setSwitch(0, true));
  changed.clear();
  f.board.tick(1);
  CHECK(f.adapter.refresh());
  bool merged = false;
  for (const auto& arguments : changed)
    merged = merged || (qvariant_cast<QModelIndex>(arguments[0]).row() <= 5
                        && qvariant_cast<QModelIndex>(arguments[1]).row() >= 6);
  CHECK(merged);  // one notification covers the adjacent watch rows
  CHECK_EQ(changed.size(), 2);  // [2,2] and [4,6]: sw's flag clears next to them
  // Every value in a snapshot comes from the same cycle and never steps time.
  const auto steps = f.engine.steps;
  CHECK(f.adapter.refresh());
  CHECK_EQ(f.engine.steps, steps);
  CHECK(cell(m, 5, SignalInspectorModel::DecimalRole).toString()
        == cell(m, 2, SignalInspectorModel::DecimalRole).toString());
}

void watches() {
  Fixture f;
  auto& m = f.inspector();
  f.adapter.setSwitch(1, true);  // sw = 2
  f.board.tick(5);                // count = 10
  CHECK(f.adapter.refresh());
  CHECK(f.adapter.addWatch(QStringLiteral("  probe.count ")));
  CHECK_EQ(m.count(), 6);
  CHECK_EQ(m.watchCount(), 1);
  CHECK(cell(m, 5, SignalInspectorModel::NameRole).toString() == QStringLiteral("probe.count"));
  CHECK_EQ(cell(m, 5, SignalInspectorModel::KindRole).toInt(), int(SignalInspectorModel::Internal));
  CHECK(cell(m, 5, SignalInspectorModel::WatchRole).toBool());
  CHECK(cell(m, 5, SignalInspectorModel::ValueRole).toString() == QStringLiteral("0x000A"));
  CHECK(!cell(m, 5, SignalInspectorModel::ChangedRole).toBool());  // no false change
  CHECK(m.watchError().isEmpty());
  // Rejections explain themselves and change nothing.
  CHECK(!f.adapter.addWatch(QStringLiteral("probe.count")));
  CHECK(m.watchError().contains(QStringLiteral("already listed")));
  CHECK(!f.adapter.addWatch(QStringLiteral("led")));
  CHECK(m.watchError().contains(QStringLiteral("Ports are listed automatically")));
  CHECK(!f.adapter.addWatch(QStringLiteral("probe.missing")));
  CHECK(m.watchError().contains(QStringLiteral("No readable signal named probe.missing")));
  CHECK(!f.adapter.addWatch(QString()));
  CHECK_EQ(m.count(), 6);
  CHECK(f.adapter.addWatch(QStringLiteral("probe.flag")));
  CHECK(m.watchError().isEmpty());
  f.board.tick(1);
  CHECK(f.adapter.refresh());
  CHECK(cell(m, 5, SignalInspectorModel::ValueRole).toString() == QStringLiteral("0x000C"));
  CHECK(cell(m, 6, SignalInspectorModel::ValueRole).toString() == QStringLiteral("0"));
  // Removal is by name and only for watches.
  CHECK(!f.adapter.removeWatch(QStringLiteral("led")));
  CHECK(f.adapter.removeWatch(QStringLiteral("probe.count")));
  CHECK_EQ(m.count(), 6);
  CHECK(cell(m, 5, SignalInspectorModel::NameRole).toString() == QStringLiteral("probe.flag"));
  f.board.tick(1);
  CHECK(f.adapter.refresh());
  CHECK(cell(m, 5, SignalInspectorModel::ValueRole).toString() == QStringLiteral("0"));  // 14 & 1
  // The watch limit, with 31 more distinct one-bit signals.
  for (int bit = 0; bit < SignalInspectorModel::MaximumWatches - 1; ++bit)
    CHECK(f.adapter.addWatch(QStringLiteral("probe.bit%1").arg(bit)));
  CHECK_EQ(m.watchCount(), SignalInspectorModel::MaximumWatches);
  CHECK(!f.adapter.addWatch(QStringLiteral("probe.count")));
  CHECK(m.watchError().contains(QStringLiteral("At most 32")));
  CHECK_EQ(m.count(), 5 + SignalInspectorModel::MaximumWatches);
  // Bit watches read the same snapshot as the counter they come from.
  f.board.tick(1);
  CHECK(f.adapter.refresh());
  const uint64_t count = f.engine.count;
  for (int bit = 0; bit < 16; ++bit) {
    const int row = rowOf(m, QStringLiteral("probe.bit%1").arg(bit));
    CHECK(cell(m, row, SignalInspectorModel::ValueRole).toString() == QString::number((count >> bit) & 1));
  }
}

void inspectorFilter() {
  Fixture f;
  auto* view = f.adapter.inspectorView();
  CHECK_EQ(view->rowCount(), 5);
  view->setFilterText(QStringLiteral("S"));
  QStringList shown;
  for (int row = 0; row < view->rowCount(); ++row)
    shown << cell(*view, row, SignalInspectorModel::NameRole).toString();
  CHECK((shown == QStringList{"spare", "sw"}));  // case-insensitive, source order
  view->setFilterText(QStringLiteral("  led "));
  CHECK_EQ(view->rowCount(), 1);
  CHECK(f.adapter.addWatch(QStringLiteral("probe.count")));
  view->setFilterText(QStringLiteral("count"));
  CHECK_EQ(view->rowCount(), 1);
  view->setFilterText(QString());
  CHECK_EQ(view->rowCount(), 6);
}

std::vector<EventLogModel::Event> modelEvents(const EventLogModel& log) {
  std::vector<EventLogModel::Event> events;
  for (int row = 0; row < log.rowCount(); ++row) {
    EventLogModel::Event event;
    event.kind = EventLogModel::Kind(cell(log, row, EventLogModel::KindRole).toInt());
    event.cycle = cell(log, row, EventLogModel::CycleRole).toString().toULongLong();
    event.text = cell(log, row, EventLogModel::TextRole).toString();
    events.push_back(event);
  }
  return events;
}

// The same stimulus on any board, refreshing the adapter every `every` cycles.
void stimulus(Fixture& f, uint64_t every) {
  const auto run = [&](uint64_t cycles) {
    while (cycles) {
      const uint64_t part = std::min(cycles, every);
      f.board.tick(part);
      cycles -= part;
      f.adapter.refresh();
    }
  };
  f.adapter.setSwitch(0, true);
  run(4'321);
  f.adapter.setButton(0, true);
  run(2'000);
  f.adapter.setSwitch(2, true);
  f.adapter.setButton(0, false);
  run(6'007);
}

void logRecordingOrderAndOwnership() {
  // Independent oracle: the board's own log under the same stimulus.
  Fixture oracle;
  oracle.board.setLogEnabled(true);
  stimulus(oracle, 100'000);
  std::vector<EventLogModel::Event> expected;
  for (const auto& line : oracle.board.structuredLog()) {
    EventLogModel::Event event;
    if (EventLogModel::parseLine(line, event)) expected.push_back(event);
  }
  CHECK(expected.size() > 5);

  for (uint64_t every : {uint64_t{100'000}, uint64_t{997}, uint64_t{1}}) {
    Fixture f;
    // Lines logged before recording belong to someone else and are cleared.
    f.board.setLogEnabled(true);
    f.adapter.setSwitch(3, true);
    CHECK(f.board.structuredLog().size() > 6);
    CHECK(f.adapter.refresh());
    CHECK_EQ(f.log().count(), 0);  // not recording: nothing drained or cleared
    CHECK(f.board.structuredLog().size() > 6);
    f.adapter.setSwitch(3, false);
    QSignalSpy inserted(f.adapter.eventLog(), &QAbstractItemModel::rowsInserted);
    CHECK(f.adapter.setLogRecording(true));
    CHECK(f.log().recording());
    CHECK_EQ(f.board.structuredLog().size(), 6);  // header only
    auto events = modelEvents(f.log());
    CHECK_EQ(events.size(), 1);
    CHECK_EQ(events[0].kind, EventLogModel::Notice);
    CHECK(events[0].text == QStringLiteral("Recording started at cycle 0"));
    const auto insertsBefore = inserted.size();
    stimulus(f, every);
    // The board's copy stays bounded: every refresh drains and clears it.
    CHECK_EQ(f.board.structuredLog().size(), 6);
    if (every == 100'000) CHECK(inserted.size() - insertsBefore <= 8);  // batched per refresh
    CHECK(f.adapter.setLogRecording(false));
    CHECK(f.board.logEnabled());  // enabled before recording, so enabled again
    events = modelEvents(f.log());
    CHECK(events.back().text == QStringLiteral("Recording stopped at cycle %1").arg(f.board.now()));
    const std::vector<EventLogModel::Event> recorded(events.begin() + 1, events.end() - 1);
    CHECK_EQ(recorded.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
      CHECK_EQ(recorded[i].cycle, expected[i].cycle);
      CHECK(recorded[i].text == expected[i].text);
      CHECK_EQ(recorded[i].kind, expected[i].kind);
    }
  }
}

// Stop and Clear act on every line logged before them, refreshed or not.
void undrainedLinesAtStopAndClear() {
  Fixture f;
  CHECK(f.adapter.setLogRecording(true));
  f.board.setSwitch(2, true);  // bypass the adapter: no refresh drains it
  f.board.tick(5);
  CHECK(f.adapter.setLogRecording(false));
  auto events = modelEvents(f.log());
  CHECK_EQ(events.size(), 3);
  CHECK(events[1].text == QStringLiteral("SW2 0->1"));
  CHECK_EQ(events[1].cycle, 0);
  CHECK(events[2].text == QStringLiteral("Recording stopped at cycle 5"));
  CHECK_EQ(f.board.structuredLog().size(), 6);

  CHECK(f.adapter.setLogRecording(true));
  f.board.setSwitch(2, false);  // logged before Clear, not yet refreshed
  CHECK(f.adapter.clearEventLog());
  CHECK_EQ(f.log().count(), 0);
  CHECK(f.adapter.refresh());
  CHECK_EQ(f.log().count(), 0);  // cleared, not shown late
  f.board.setSwitch(1, true);
  CHECK(f.adapter.refresh());
  events = modelEvents(f.log());
  CHECK_EQ(events.size(), 1);
  CHECK(events[0].text == QStringLiteral("SW1 0->1"));

  // Once stopped, the board's log belongs to its owner again: Clear empties
  // only the view.
  Fixture g;
  g.board.setLogEnabled(true);  // an owner that logs
  CHECK(g.adapter.setLogRecording(true));
  CHECK(g.adapter.setLogRecording(false));
  g.board.setSwitch(3, true);
  g.board.tick(3'000);
  const auto owned = g.board.structuredLog();
  CHECK(owned.size() > 6);
  CHECK(g.adapter.clearEventLog());
  CHECK_EQ(g.log().count(), 0);
  CHECK(g.board.structuredLog() == owned);
  CHECK(g.board.logEnabled());
}

void logEnableStateIsRestored() {
  Fixture f;
  f.board.setLogEnabled(true);
  CHECK(f.adapter.setLogRecording(true));
  CHECK(f.adapter.setLogRecording(true));  // idempotent
  CHECK(f.adapter.setLogRecording(false));
  CHECK(f.board.logEnabled());  // was enabled before recording
  Fixture g;
  CHECK(g.adapter.setLogRecording(true));
  CHECK(g.board.logEnabled());
  CHECK(g.adapter.setLogRecording(false));
  CHECK(!g.board.logEnabled());
  // Destroying the adapter while recording restores the board's state.
  {
    ProbeEngine engine;
    BoardModel board(engine, PinBinding::bind(parseXdc(probeXdc()), engine));
    {
      BoardAdapter adapter(&board);
      CHECK(adapter.setLogRecording(true));
      CHECK(board.logEnabled());
      board.setSwitch(0, true);  // undrained: the view's line, not the owner's
    }
    CHECK(!board.logEnabled());
    CHECK_EQ(board.structuredLog().size(), 6);
  }
  // Destroying a stopped view leaves the owner's lines alone.
  {
    ProbeEngine engine;
    BoardModel board(engine, PinBinding::bind(parseXdc(probeXdc()), engine));
    board.setLogEnabled(true);
    {
      BoardAdapter adapter(&board);
      CHECK(adapter.setLogRecording(true));
      CHECK(adapter.setLogRecording(false));
      board.setSwitch(0, true);
      board.tick(2'500);
    }
    CHECK(board.logEnabled());
    CHECK(board.structuredLog().size() > 6);  // the switch and grid-observed LED lines
  }
}

void categoriesParsingAndFilter() {
  EventLogModel::Event event;
  CHECK(!EventLogModel::parseLine("# virtualbasys-log v=2", event));
  CHECK(!EventLogModel::parseLine("[cycle ] LED[0] 0->1", event));
  CHECK(!EventLogModel::parseLine("[cycle 12x] LED[0] 0->1", event));
  CHECK(!EventLogModel::parseLine("[cycle:12] LED[0] 0->1", event));  // wrong prefix, digits in place
  CHECK(!EventLogModel::parseLine("[cycle 18446744073709551616] LED[0] 0->1", event));  // > uint64
  CHECK(!EventLogModel::parseLine("[cycle 99999999999999999999] LED[0] 0->1", event));
  CHECK(EventLogModel::parseLine("[cycle 18446744073709551615] UART TX 0x41 'A'", event));
  CHECK_EQ(event.cycle, 18446744073709551615ull);
  CHECK(event.text == QStringLiteral("UART TX 0x41 'A'"));
  const std::vector<std::pair<const char*, EventLogModel::Kind>> kinds{
      {"SSEG[0] ' '->'0'", EventLogModel::Display}, {"SSEG[2].dp 0->1", EventLogModel::Display},
      {"LED[15] 1->0", EventLogModel::Leds}, {"UART RX 0x68 'h'", EventLogModel::Uart},
      {"UART TX framing error", EventLogModel::Uart}, {"SW3 0->1", EventLogModel::Inputs},
      {"BTNC 1->0", EventLogModel::Inputs}, {"something new", EventLogModel::Other}};
  for (const auto& [body, kind] : kinds) CHECK_EQ(EventLogModel::kindOf(QString::fromLatin1(body)), kind);

  Fixture f;
  CHECK(f.adapter.setLogRecording(true));
  f.adapter.setSwitch(0, true);
  f.adapter.setButton(0, true);
  f.board.tick(10);
  f.adapter.setSwitch(0, false);
  CHECK(f.adapter.refresh());
  auto* view = f.adapter.eventLogView();
  CHECK_EQ(view->rowCount(), 4);  // notice + 3 input events
  view->setShowInputs(false);
  CHECK_EQ(view->rowCount(), 1);  // notices always pass the category filter
  view->setShowInputs(true);
  view->setFilterText(QStringLiteral("sw0"));
  CHECK_EQ(view->rowCount(), 2);
  CHECK(cell(*view, 0, EventLogModel::CycleRole).toString() == QStringLiteral("0"));
  CHECK(cell(*view, 1, EventLogModel::CycleRole).toString() == QStringLiteral("10"));
  view->setFilterText(QStringLiteral("10"));  // cycles are searchable
  CHECK_EQ(view->rowCount(), 1);
  view->setFilterText(QString());
  // Clearing the view keeps recording.
  CHECK(f.adapter.clearEventLog());
  CHECK_EQ(f.log().count(), 0);
  CHECK(f.log().recording());
  f.adapter.setSwitch(1, true);
  CHECK(f.adapter.refresh());
  CHECK_EQ(f.log().count(), 1);
}

void boundedLog() {
  Fixture f;
  CHECK(f.adapter.setLogRecording(true));
  QSignalSpy removed(f.adapter.eventLog(), &QAbstractItemModel::rowsRemoved);
  // Switch toggles without advancing time: exactly one event each (no LED
  // observation grid is crossed). Switch event i reads "0->1" for even i.
  for (int i = 0; i < EventLogModel::MaximumEvents + 499; ++i)
    f.board.setSwitch(0, i % 2 == 0);  // bypass the adapter: one batch below
  CHECK(f.adapter.refresh());  // one oversized batch: nothing inserted then trimmed
  CHECK_EQ(f.log().count(), EventLogModel::MaximumEvents);
  CHECK_EQ(f.log().trimmedEvents(), 500);  // the start notice and events 0..498
  CHECK_EQ(f.log().recordedEvents(), EventLogModel::MaximumEvents + 500);
  // Only the pre-existing notice row is removed; incoming rows that would be
  // trimmed at once are never inserted.
  CHECK_EQ(removed.size(), 1);
  CHECK_EQ(removed[0][1].toInt(), 0);
  CHECK_EQ(removed[0][2].toInt(), 0);
  removed.clear();
  CHECK(cell(f.log(), 0, EventLogModel::TextRole).toString() == QStringLiteral("SW0 1->0"));  // event 499
  CHECK(cell(f.log(), EventLogModel::MaximumEvents - 1, EventLogModel::TextRole).toString()
        == QStringLiteral("SW0 0->1"));  // event 10,498
  // Later small batches trim from the front.
  f.board.setSwitch(0, false);
  f.board.setSwitch(0, true);
  CHECK(f.adapter.refresh());
  CHECK_EQ(f.log().count(), EventLogModel::MaximumEvents);
  CHECK_EQ(f.log().trimmedEvents(), 502);
  CHECK(!removed.isEmpty());
  CHECK(cell(f.log(), 0, EventLogModel::TextRole).toString() == QStringLiteral("SW0 1->0"));  // event 501 (odd)
  // Clear starts over: no rows and no discarded count.
  CHECK(f.adapter.clearEventLog());
  CHECK_EQ(f.log().count(), 0);
  CHECK_EQ(f.log().trimmedEvents(), 0);
  CHECK(f.log().recording());
}

void guards() {
  Fixture f;
  bool accepted = true;
  std::thread worker([&] {
    accepted = f.adapter.addWatch(QStringLiteral("probe.count"))
        || f.adapter.removeWatch(QStringLiteral("probe.count"))
        || f.adapter.setLogRecording(true) || f.adapter.clearEventLog();
  });
  worker.join();
  CHECK(!accepted);
  CHECK_EQ(f.inspector().count(), 5);
  CHECK(!f.log().recording());

  CHECK(f.adapter.setLogRecording(true));
  int nested = 0;
  int valueHandlers = 0;
  int logHandlers = 0;
  const auto attempt = [&] {
    nested += f.adapter.addWatch(QStringLiteral("probe.flag")) + f.adapter.setLogRecording(false)
        + f.adapter.clearEventLog() + f.adapter.refresh() + f.adapter.setSwitch(0, true);
  };
  QObject::connect(f.adapter.inspector(), &QAbstractItemModel::dataChanged, f.adapter.inspector(),
                   [&] { ++valueHandlers; attempt(); });
  QObject::connect(f.adapter.eventLog(), &QAbstractItemModel::rowsInserted, f.adapter.eventLog(),
                   [&] { ++logHandlers; attempt(); });
  f.board.setSwitch(1, true);
  f.board.tick(3);
  CHECK(f.adapter.refresh());
  CHECK(valueHandlers > 0 && logHandlers > 0);  // the attempts really ran
  CHECK_EQ(nested, 0);
  CHECK(f.log().recording());
  CHECK_EQ(f.inspector().watchCount(), 0);

  // SignalIds never reach QML: no role or property carries one.
  const auto roles = f.inspector().roleNames();
  for (const auto& name : roles) CHECK(!name.contains("id"));
  const QMetaObject& meta = SignalInspectorModel::staticMetaObject;
  for (int i = meta.methodOffset(); i < meta.methodCount(); ++i)
    CHECK(meta.method(i).methodType() == QMetaMethod::Signal);
  for (const char* method : {"addWatch(QString)", "removeWatch(QString)",
                             "setLogRecording(bool)", "clearEventLog()"})
    CHECK(BoardAdapter::staticMetaObject.indexOfMethod(method) >= 0);

  // Disconnected adapters are unavailable and reject everything.
  BoardAdapter empty;
  CHECK(!empty.inspector()->available() && !empty.eventLog()->available());
  CHECK_EQ(empty.inspector()->count(), 0);
  CHECK(!empty.addWatch(QStringLiteral("probe.count")));
  CHECK(!empty.setLogRecording(true));
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  portsKindsBindingsAndValues();
  pinSummariesNeverMislead();
  batchedChangesAtOneCycle();
  watches();
  inspectorFilter();
  logRecordingOrderAndOwnership();
  undrainedLinesAtStopAndClear();
  logEnableStateIsRestored();
  categoriesParsingAndFilter();
  boundedLog();
  guards();
  std::puts("test_qt_inspector_log: PASS");
  return 0;
}
