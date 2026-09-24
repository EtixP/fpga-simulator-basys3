// UART terminal seam: BoardModel bytes/stamps -> BoardAdapter -> bounded
// UartConsoleModel. Serial waveforms and expected grid stamps are generated
// analytically here, independent of the board's decoder and the adapter.
#include "board/BoardModel.h"
#include "check.h"
#include "qt/BoardAdapter.h"
#include "qt/SimulationController.h"
#include "qt/UartConsoleModel.h"

#include <QAbstractItemModelTester>
#include <QCoreApplication>
#include <QDebug>
#include <QMetaMethod>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QtQml/qqmlextensionplugin.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

Q_IMPORT_QML_PLUGIN(VirtualBasys_BoardPlugin)

using namespace vb;
using namespace vb::qt;

namespace {
constexpr uint64_t kGrid = BoardModel::kSampleChunkCycles;
constexpr uint64_t kBit = kUartCyclesPerBit;
constexpr uint64_t kFrame = 10 * kBit;

// Frames of the design's TX line; overlapping idle-high segments combine by AND.
struct Segment {
  uint64_t start;
  std::vector<uint8_t> bytes;
  bool badStop = false;  // stop bit of the first frame driven low
};

bool serialLevel(uint64_t now, const Segment& segment) {
  if (now < segment.start || now - segment.start >= segment.bytes.size() * kFrame) return true;
  const uint64_t frame = (now - segment.start) / kFrame;
  const uint64_t index = ((now - segment.start) % kFrame) / kBit;
  if (index == 0) return false;
  if (index == 9) return !(segment.badStop && frame == 0);
  return (segment.bytes[frame] >> (index - 1)) & 1;
}

// The decoder observes a start on the first grid at/after the falling edge and
// samples the stop bit on the first grid at/after that observation + 9.5 bits.
uint64_t stopSample(uint64_t start) {
  const uint64_t observed = (start + kGrid - 1) / kGrid * kGrid;
  return (observed + 9 * kBit + kBit / 2 + kGrid - 1) / kGrid * kGrid;
}

class SerialEngine final : public SimEngine {
public:
  enum Port : uint32_t { Rx, Button, Tx };
  std::vector<Segment> segments;
  std::vector<std::pair<uint64_t, uint64_t>> rxPokes;
  std::vector<std::pair<uint64_t, uint64_t>> buttonPokes;
  unsigned steps = 0;
  bool failSteps = false;

  SignalId lookup(std::string_view name) override {
    for (size_t i = 0; i < metadata_.size(); ++i)
      if (metadata_[i].name == name) return SignalId(i);
    return kNoSignal;
  }
  SignalInfo info(SignalId id) const override { return metadata_.at(size_t(id)); }
  std::vector<SignalInfo> ports() const override { return metadata_; }
  void step(uint64_t cycles) override {
    if (failSteps) throw std::runtime_error("synthetic engine failure");
    ++steps;
    now_ += cycles;
  }
  uint64_t now() const override { return now_; }
  uint64_t peek(SignalId id) override {
    if (size_t(id) == Tx) {
      bool level = true;
      for (const auto& segment : segments) level = level && serialLevel(now_, segment);
      return level;
    }
    if (size_t(id) == Rx) return rx_;
    if (size_t(id) == Button) return button_;
    throw std::invalid_argument("invalid signal");
  }
  void poke(SignalId id, uint64_t value) override {
    if (size_t(id) == Rx) {
      rx_ = value & 1;
      rxPokes.emplace_back(now_, rx_);
    } else if (size_t(id) == Button) {
      button_ = value & 1;
      buttonPokes.emplace_back(now_, button_);
    } else {
      throw std::invalid_argument("not an input");
    }
  }
  void setTraceFile(std::string_view) override { CHECK(false); }
  void trace(bool) override { CHECK(false); }

private:
  uint64_t now_ = 0;
  uint64_t rx_ = 0;
  uint64_t button_ = 0;
  const std::vector<SignalInfo> metadata_{
      {"serial_in", 1, true}, {"reset", 1, true}, {"serial_out", 1, false}};
};

enum class Pins { Both, TxOnly, RxOnly, None };

PinBinding binding(SerialEngine& engine, Pins pins) {
  std::string xdc = "set_property PACKAGE_PIN U18 [get_ports reset]\n";
  if (pins == Pins::Both || pins == Pins::RxOnly)
    xdc += "set_property PACKAGE_PIN B18 [get_ports serial_in]\n";
  if (pins == Pins::Both || pins == Pins::TxOnly)
    xdc += "set_property PACKAGE_PIN A18 [get_ports serial_out]\n";
  auto result = PinBinding::bind(parseXdc(xdc), engine);
  // Only the deliberately unconstrained serial ports may be reported.
  const size_t unbound = pins == Pins::Both ? 0 : pins == Pins::None ? 2 : 1;
  CHECK_EQ(result.diagnostics().size(), unbound);
  for (const auto& diagnostic : result.diagnostics())
    CHECK(diagnostic.find("has no pin constraint") != std::string::npos);
  return result;
}

struct Fixture {
  explicit Fixture(Pins pins = Pins::Both) : board(engine, binding(engine, pins)) {
    // The board drives the receive line's idle-high level once at construction.
    const bool rx = pins == Pins::Both || pins == Pins::RxOnly;
    CHECK_EQ(engine.rxPokes.size(), rx ? size_t{1} : size_t{0});
    engine.rxPokes.clear();
  }
  SerialEngine engine;
  BoardModel board;
  BoardAdapter adapter{&board};
  QAbstractItemModelTester tester{adapter.uart(),
                                  QAbstractItemModelTester::FailureReportingMode::Fatal};
  UartConsoleModel& uart() { return *adapter.uart(); }
  void runTo(uint64_t cycle) {
    CHECK(cycle >= board.now());
    board.tick(cycle - board.now());
  }
};

SimulationController::Options manualOptions() {
  SimulationController::Options options;
  options.automaticScheduling = false;
  options.nowNanoseconds = [] { return qint64{0}; };
  return options;
}

struct Row {
  int kind;
  QString text;
  QString hex;
  int bytes;
  QString cycle;
  QString lastCycle;
  QString time;
  bool operator==(const Row&) const = default;
};

Row row(const UartConsoleModel& model, int index) {
  const auto at = model.index(index, 0);
  return {model.data(at, UartConsoleModel::KindRole).toInt(),
          model.data(at, UartConsoleModel::TextRole).toString(),
          model.data(at, UartConsoleModel::HexRole).toString(),
          model.data(at, UartConsoleModel::ByteCountRole).toInt(),
          model.data(at, UartConsoleModel::CycleRole).toString(),
          model.data(at, UartConsoleModel::LastCycleRole).toString(),
          model.data(at, UartConsoleModel::TimeRole).toString()};
}

std::vector<Row> rows(const UartConsoleModel& model) {
  std::vector<Row> result;
  for (int i = 0; i < model.rowCount(); ++i) result.push_back(row(model, i));
  return result;
}

QString number(uint64_t value) { return QString::number(value); }

// Exact bytes shown by every TX or RX row, in row order.
QByteArray shownBytes(const UartConsoleModel& model, UartConsoleModel::Kind kind) {
  QByteArray bytes;
  for (const auto& line : rows(model)) {
    if (line.kind != kind) continue;
    const auto decoded = QByteArray::fromHex(line.hex.toLatin1());
    CHECK_EQ(decoded.size(), line.bytes);
    bytes += decoded;
  }
  return bytes;
}

void disconnectedAndAvailability() {
  BoardAdapter empty;
  auto* uart = empty.uart();
  CHECK(uart != nullptr);
  CHECK(uart->parent() == &empty);
  CHECK(!uart->available() && !uart->txBound() && !uart->rxBound());
  CHECK_EQ(uart->rowCount(), 0);
  CHECK(!empty.sendUartText(QStringLiteral("x")));
  CHECK(uart->sendError().isEmpty());  // no design, so nothing to explain
  CHECK(empty.clearUart());

  const auto roles = uart->roleNames();
  CHECK(roles.value(UartConsoleModel::KindRole) == "kind");
  CHECK(roles.value(UartConsoleModel::TextRole) == "text");
  CHECK(roles.value(UartConsoleModel::HexRole) == "hex");
  CHECK(roles.value(UartConsoleModel::ByteCountRole) == "byteCount");
  CHECK(roles.value(UartConsoleModel::CycleRole) == "cycle");
  CHECK(roles.value(UartConsoleModel::LastCycleRole) == "lastCycle");
  CHECK(roles.value(UartConsoleModel::TimeRole) == "time");

  for (Pins pins : {Pins::Both, Pins::TxOnly, Pins::RxOnly, Pins::None}) {
    Fixture f(pins);
    const bool tx = pins == Pins::Both || pins == Pins::TxOnly;
    const bool rx = pins == Pins::Both || pins == Pins::RxOnly;
    CHECK_EQ(f.uart().txBound(), tx);
    CHECK_EQ(f.uart().rxBound(), rx);
    CHECK_EQ(f.uart().available(), tx || rx);
    CHECK_EQ(f.adapter.sendUartText(QStringLiteral("ok")), rx);
    if (!rx) {
      CHECK(f.uart().sendError().contains(QStringLiteral("RsRx")));
      CHECK_EQ(f.uart().rxBytes(), 0);
    }
    f.runTo(10 * kFrame);
    CHECK(f.adapter.refresh());
    // A reset marker needs a UART to annotate.
    SimulationController controller(f.adapter, QStringLiteral("Pins"), manualOptions());
    CHECK(controller.reset());
    CHECK_EQ(f.uart().rowCount(), rx ? 2 : (tx ? 1 : 0));
  }
}

void qmlExposure() {
  // Mutation stays on BoardAdapter; the model exposes no invokable methods.
  const QMetaObject& meta = UartConsoleModel::staticMetaObject;
  for (int i = meta.methodOffset(); i < meta.methodCount(); ++i)
    CHECK(meta.method(i).methodType() == QMetaMethod::Signal);
  for (const char* hidden : {"stageCounters", "appendTraffic", "appendNotice",
                             "setSendError", "clearLines", "noteReset", "stageUart"}) {
    CHECK_EQ(UartConsoleModel::staticMetaObject.indexOfMethod(hidden), -1);
    CHECK_EQ(BoardAdapter::staticMetaObject.indexOfMethod(hidden), -1);
  }
  CHECK(BoardAdapter::staticMetaObject.indexOfMethod("sendUartText(QString)") >= 0);
  CHECK(BoardAdapter::staticMetaObject.indexOfMethod("clearUart()") >= 0);

  Fixture f;
  CHECK(f.adapter.sendUartText(QStringLiteral("q")));
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
    import QtQml
    import VirtualBasys.Board
    QtObject {
      required property BoardAdapter board
      readonly property UartConsoleModel uart: board.uart
      property int rows: uart.count
      property bool bound: uart.txBound && uart.rxBound
      property int rx: uart.rxBytes
      property int queued: uart.pendingRxBytes
      property int notice: UartConsoleModel.Notice
      property bool sent: board.sendUartText("r")
    })", QUrl());
  std::unique_ptr<QObject> object(component.createWithInitialProperties(
      {{QStringLiteral("board"), QVariant::fromValue(&f.adapter)}}));
  if (!object) qWarning() << component.errors();
  CHECK(object != nullptr);
  CHECK(object->property("bound").toBool());
  CHECK(object->property("sent").toBool());
  CHECK_EQ(object->property("rows").toInt(), 2);  // one row per send
  CHECK_EQ(object->property("rx").toInt(), 2);
  CHECK_EQ(object->property("queued").toInt(), 2);
  CHECK_EQ(object->property("notice").toInt(), int(UartConsoleModel::Notice));
  CHECK(row(f.uart(), 1).text == QStringLiteral("r"));
  CHECK(row(f.uart(), 1).cycle == number(kFrame));
}

// Terminal rows depend only on the board's bytes, never on refresh cadence.
std::vector<Row> runOutput(uint64_t refreshEvery) {
  Fixture f;
  f.engine.segments = {{12'345, {'h', 'i', '\r', '\n', 0x00, 0xFF, '\\', '\t', '\r', '!'}},
                       {2'000'000, {'o', 'k', '\n', '\n'}}};
  int inserted = 0;
  int changed = 0;
  auto* model = f.adapter.uart();
  QObject::connect(model, &QAbstractItemModel::rowsInserted, model, [&] { ++inserted; });
  // Growing an open row updates only that final row's byte-dependent roles.
  QObject::connect(model, &QAbstractItemModel::dataChanged, model,
                   [&](const QModelIndex& first, const QModelIndex& last, const QList<int>& roles) {
                     ++changed;
                     CHECK(first == last);
                     CHECK_EQ(first.row(), model->rowCount() - 1);
                     CHECK(roles.contains(UartConsoleModel::TextRole));
                     CHECK(roles.contains(UartConsoleModel::HexRole));
                     CHECK(roles.contains(UartConsoleModel::ByteCountRole));
                     CHECK(roles.contains(UartConsoleModel::LastCycleRole));
                     CHECK(!roles.contains(UartConsoleModel::CycleRole));
                   });
  constexpr uint64_t end = 3'000'000;
  while (f.board.now() < end) {
    f.board.tick(std::min(refreshEvery, end - f.board.now()));
    CHECK(f.adapter.refresh());
  }
  if (refreshEvery >= end) {
    CHECK_EQ(inserted, 1);
    CHECK_EQ(changed, 0);
  } else {
    CHECK(changed > 0);
  }
  CHECK_EQ(f.uart().txBytes(), 14);
  CHECK_EQ(f.uart().rxBytes(), 0);
  return rows(f.uart());
}

void outputRowsAndStamps() {
  const auto whole = runOutput(3'000'000);
  CHECK(whole == runOutput(997));
  CHECK(whole == runOutput(kGrid));

  std::vector<uint64_t> stamps;
  for (uint64_t i = 0; i < 10; ++i) stamps.push_back(stopSample(12'345 + i * kFrame));
  for (uint64_t i = 0; i < 4; ++i) stamps.push_back(stopSample(2'000'000 + i * kFrame));
  CHECK_EQ(whole.size(), 3);
  for (const auto& line : whole) CHECK_EQ(line.kind, int(UartConsoleModel::Tx));
  // CRLF ends a row and is hidden from text, never from hex.
  CHECK(whole[0].text == QStringLiteral("hi"));
  CHECK(whole[0].hex == QStringLiteral("68 69 0D 0A"));
  CHECK_EQ(whole[0].bytes, 4);
  CHECK(whole[0].cycle == number(stamps[0]));
  CHECK(whole[0].lastCycle == number(stamps[3]));
  // Exact integer time: stamp * 10 ns, with nine fractional digits.
  CHECK(whole[0].time == QStringLiteral("0.%1 s").arg(stamps[0] * 10, 9, 10, QLatin1Char('0')));
  // Non-printables escape; a lone CR is visible; a backslash stays literal
  // (the hex view disambiguates it). An unterminated line continues across
  // the idle gap until its newline.
  CHECK(whole[1].text == QStringLiteral("\\x00\\xFF\\\\t\\r!ok"));
  CHECK(whole[1].hex == QStringLiteral("00 FF 5C 09 0D 21 6F 6B 0A"));
  CHECK_EQ(whole[1].bytes, 9);
  CHECK(whole[1].cycle == number(stamps[4]));
  CHECK(whole[1].lastCycle == number(stamps[12]));
  CHECK(whole[1].time == QStringLiteral("0.%1 s").arg(stamps[4] * 10, 9, 10, QLatin1Char('0')));
  // A bare newline is an empty, terminated line.
  CHECK(whole[2].text.isEmpty() && whole[2].hex == QStringLiteral("0A"));
  CHECK(whole[2].cycle == number(stamps[13]) && whole[2].lastCycle == number(stamps[13]));
}

void allBytesAndLongLines() {
  Fixture f;
  std::vector<uint8_t> bytes;
  for (int value = 0; value < 256; ++value) bytes.push_back(uint8_t(value));
  for (int value = 255; value >= 0; --value) bytes.push_back(uint8_t(value));
  f.engine.segments = {{4'321, bytes}};
  f.runTo(4'321 + bytes.size() * kFrame + 2 * kFrame);
  CHECK(f.adapter.refresh());
  // Byte order and values survive exactly; nothing is added or dropped.
  CHECK(shownBytes(f.uart(), UartConsoleModel::Tx)
        == QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size())));
  CHECK_EQ(f.uart().txBytes(), 512);
  CHECK(f.board.uartTxBytes() == bytes);
  // Rows end after 0x0A or at 128 bytes; each is stamped with its first byte.
  const std::vector<int> lengths{11, 128, 128, 128, 107, 10};
  const auto all = rows(f.uart());
  CHECK_EQ(all.size(), lengths.size());
  uint64_t index = 0;
  for (size_t i = 0; i < all.size(); ++i) {
    CHECK_EQ(all[i].bytes, lengths[i]);
    CHECK(all[i].cycle == number(stopSample(4'321 + index * kFrame)));
    index += uint64_t(lengths[i]);
    CHECK(all[i].lastCycle == number(stopSample(4'321 + (index - 1) * kFrame)));
  }
  CHECK(all[0].text == QStringLiteral(
      "\\x00\\x01\\x02\\x03\\x04\\x05\\x06\\x07\\x08\\t"));
  CHECK(all[1].text.startsWith(QStringLiteral("\\x0B\\x0C\\r\\x0E")));
  CHECK(all[1].text.contains(QStringLiteral(" !\"#$%&'()*+,-./0123456789:;<=>?@ABC")));
  CHECK(all[1].text.contains(QStringLiteral("xyz{|}~\\x7F\\x80")));
  CHECK(all[2].text.startsWith(QStringLiteral("\\x8B")));
}

void inputQueueAndOrdering() {
  Fixture f;
  f.engine.segments = {{1'000, {'<', '>'}}};
  f.runTo(3 * kFrame);
  // Decoded but not yet published output precedes later input.
  const auto echoed = f.board.uartTxByteCycles();
  CHECK_EQ(echoed.size(), 2);
  const uint64_t queued = f.board.now();
  CHECK(f.adapter.sendUartText(QStringLiteral("ab\ncd")));
  const auto all = rows(f.uart());
  CHECK_EQ(all.size(), 3);
  CHECK_EQ(all[0].kind, int(UartConsoleModel::Tx));
  CHECK(all[0].text == QStringLiteral("<>"));
  CHECK(all[0].cycle == number(echoed[0]));
  CHECK_EQ(all[1].kind, int(UartConsoleModel::Rx));
  CHECK(all[1].text == QStringLiteral("ab"));
  CHECK(all[1].hex == QStringLiteral("61 62 0A"));
  CHECK(all[1].cycle == number(queued));
  CHECK(all[1].lastCycle == number(queued + 2 * kFrame));
  CHECK(all[2].text == QStringLiteral("cd"));
  CHECK(all[2].cycle == number(queued + 3 * kFrame));
  CHECK(all[2].lastCycle == number(queued + 4 * kFrame));
  CHECK_EQ(f.uart().rxBytes(), 5);
  CHECK_EQ(f.uart().pendingRxBytes(), 5);
  CHECK(f.uart().sendError().isEmpty());
  // Queuing drives nothing and advances nothing until the board ticks.
  CHECK(f.engine.rxPokes.empty());
  CHECK_EQ(f.board.now(), queued);
  f.board.tick(0);
  CHECK_EQ(f.engine.rxPokes.size(), 1);
  CHECK(f.engine.rxPokes[0] == std::make_pair(queued, uint64_t{0}));

  // Pending counts frames still occupying or awaiting the line.
  f.runTo(queued + kFrame - 1);
  CHECK(f.adapter.refresh());
  CHECK_EQ(f.uart().pendingRxBytes(), 5);
  f.runTo(queued + kFrame);
  CHECK(f.adapter.refresh());
  CHECK_EQ(f.uart().pendingRxBytes(), 4);

  // A send during the final stop bit waits for the line; its row says when.
  f.runTo(queued + 5 * kFrame - 17);
  CHECK(f.adapter.sendUartText(QString::fromUtf8("\xC3\xA9")));
  const auto accent = row(f.uart(), f.uart().rowCount() - 1);
  CHECK_EQ(accent.kind, int(UartConsoleModel::Rx));
  CHECK(accent.hex == QStringLiteral("C3 A9"));
  CHECK(accent.text == QStringLiteral("\\xC3\\xA9"));
  CHECK(accent.cycle == number(queued + 5 * kFrame));
  CHECK_EQ(f.uart().pendingRxBytes(), 3);
  // Each send starts its own row, even after unterminated input.
  CHECK(f.adapter.sendUartText(QStringLiteral("!")));
  CHECK_EQ(f.uart().rowCount(), 5);
  CHECK(row(f.uart(), 3).hex == QStringLiteral("C3 A9"));
  CHECK(row(f.uart(), 4).hex == QStringLiteral("21"));
  CHECK(row(f.uart(), 4).cycle == number(queued + 7 * kFrame));
  CHECK(!f.adapter.sendUartText(QString()));
  CHECK(f.uart().sendError().isEmpty());
  CHECK_EQ(f.uart().rxBytes(), 8);

  // The board drives the queued bytes with its own frame schedule.
  f.runTo(queued + 20 * kFrame);
  CHECK(f.adapter.refresh());
  CHECK_EQ(f.uart().pendingRxBytes(), 0);
  std::vector<uint64_t> starts;
  for (size_t i = 0; i < f.engine.rxPokes.size(); ++i)
    if (f.engine.rxPokes[i].second == 0 && (i == 0 || f.engine.rxPokes[i - 1].second == 1))
      starts.push_back(f.engine.rxPokes[i].first);
  CHECK_EQ(starts.front(), queued);
}

void boundedPendingQueue() {
  Fixture f;
  CHECK(f.adapter.sendUartText(QString(4000, QLatin1Char('a'))));
  CHECK_EQ(f.uart().pendingRxBytes(), 4000);
  CHECK_EQ(f.uart().rowCount(), 32);  // 31 full 128-byte rows + 32 bytes
  CHECK_EQ(row(f.uart(), 31).bytes, 32);
  QSignalSpy errors(f.adapter.uart(), &UartConsoleModel::sendErrorChanged);
  CHECK(!f.adapter.sendUartText(QString(97, QLatin1Char('b'))));
  CHECK_EQ(errors.size(), 1);
  CHECK(f.uart().sendError().contains(QStringLiteral("needs 97 B")));
  CHECK(f.uart().sendError().contains(QStringLiteral("only 96 of 4096 B")));
  CHECK_EQ(f.uart().rxBytes(), 4000);  // all or nothing
  CHECK_EQ(f.uart().rowCount(), 32);
  CHECK(f.adapter.sendUartText(QString(96, QLatin1Char('b'))));
  CHECK(f.uart().sendError().isEmpty());
  CHECK_EQ(errors.size(), 2);
  CHECK_EQ(f.uart().rowCount(), 33);
  CHECK_EQ(f.uart().pendingRxBytes(), 4096);
  CHECK(!f.adapter.sendUartText(QStringLiteral("c")));
  // Space returns only as virtual time completes frames.
  f.runTo(kFrame);
  CHECK(f.adapter.sendUartText(QStringLiteral("c")));
  CHECK(!f.adapter.sendUartText(QStringLiteral("d")));
  CHECK(!f.adapter.sendUartText(QString(5000, QLatin1Char('e'))));
  // A text larger than the whole queue gets advice that can actually help.
  CHECK(f.uart().sendError().contains(QStringLiteral("this text is 5000 B")));
  CHECK(f.uart().sendError().contains(QStringLiteral("at most 4096 B")));
  CHECK(!f.uart().sendError().contains(QStringLiteral("Run or step")));
  CHECK(f.adapter.clearUart());
  CHECK(f.uart().sendError().isEmpty());
  CHECK_EQ(f.uart().pendingRxBytes(), 4096);  // clearing the view keeps input
  // Four-byte UTF-8 characters count as bytes, not characters.
  f.runTo(3 * kFrame);
  CHECK(!f.adapter.sendUartText(QString::fromUtf8("\xF0\x9F\x98\x80")));
  CHECK(f.uart().sendError().contains(QStringLiteral("needs 4 B")));
  // Text UTF-8 cannot represent is rejected whole with a reason, never
  // sent partly: toUtf8() would silently drop the unpaired surrogate.
  f.runTo(20 * kFrame);
  const auto queuedBefore = f.uart().rxBytes();
  for (const QString& text : {QString(QChar(0xD800)),
                              QStringLiteral("a") + QChar(0xD800) + QStringLiteral("b")}) {
    CHECK(f.adapter.clearUart());
    CHECK(!f.adapter.sendUartText(text));
    CHECK(f.uart().sendError().contains(QStringLiteral("cannot be encoded as UTF-8")));
    CHECK_EQ(f.uart().rxBytes(), queuedBefore);
  }
  // A valid surrogate pair is ordinary four-byte UTF-8, and a leading U+FEFF
  // (a byte-order mark in pasted text) is sent as its bytes, not stripped.
  CHECK(f.adapter.sendUartText(QString::fromUtf8("\xF0\x9F\x98\x80")));
  CHECK(f.uart().sendError().isEmpty());
  CHECK(f.adapter.sendUartText(QChar(0xFEFF) + QStringLiteral("ok")));
  CHECK(row(f.uart(), f.uart().rowCount() - 1).hex == QStringLiteral("EF BB BF 6F 6B"));
  CHECK(f.uart().sendError().isEmpty());
}

void framingErrorNotices() {
  Fixture f;
  f.engine.segments = {{10'000, {'A'}}, {300'000, {0x5A}, true}, {600'000, {'B', '\n'}}};
  f.runTo(900'000);
  CHECK(f.adapter.refresh());
  const auto all = rows(f.uart());
  CHECK_EQ(all.size(), 3);
  CHECK(all[0].text == QStringLiteral("A") && all[0].cycle == number(stopSample(10'000)));
  CHECK_EQ(all[1].kind, int(UartConsoleModel::Notice));
  CHECK(all[1].text == QStringLiteral("Framing error: stop bit sampled low"));
  CHECK(all[1].cycle == number(stopSample(300'000)));
  CHECK(all[1].hex.isEmpty() && all[1].bytes == 0);
  // The notice ends the open line; later output starts a new row.
  CHECK(all[2].text == QStringLiteral("B") && all[2].cycle == number(stopSample(600'000)));
  CHECK_EQ(f.uart().framingErrors(), 1);
  CHECK_EQ(f.uart().txBytes(), 3);

  // Each error has its own notice at its own stamp, even when several are
  // decoded between refreshes: the rows never depend on refresh cadence.
  const std::vector<Segment> noisy{{1'000'000, {0x11}, true}, {1'150'000, {'x'}},
                                   {1'300'000, {0x22}, true}, {1'600'000, {'C'}}};
  f.engine.segments = noisy;
  f.runTo(1'900'000);
  CHECK(f.adapter.refresh());
  const auto later = rows(f.uart());
  CHECK_EQ(later.size(), 7);
  CHECK_EQ(later[3].kind, int(UartConsoleModel::Notice));
  CHECK(later[3].cycle == number(stopSample(1'000'000)));
  CHECK(later[4].text == QStringLiteral("x"));
  CHECK_EQ(later[5].kind, int(UartConsoleModel::Notice));
  CHECK(later[5].cycle == number(stopSample(1'300'000)));
  CHECK(later[6].text == QStringLiteral("C"));
  CHECK_EQ(f.uart().framingErrors(), 3);
  for (uint64_t every : {uint64_t{kGrid}, uint64_t{37'111}}) {
    Fixture g;
    g.engine.segments = {{10'000, {'A'}}, {300'000, {0x5A}, true}, {600'000, {'B', '\n'}}};
    while (g.board.now() < 900'000) {
      g.board.tick(std::min(every, 900'000 - g.board.now()));
      CHECK(g.adapter.refresh());
    }
    g.engine.segments = noisy;
    while (g.board.now() < 1'900'000) {
      g.board.tick(std::min(every, 1'900'000 - g.board.now()));
      CHECK(g.adapter.refresh());
    }
    CHECK(rows(g.uart()) == later);
  }
}

void boundedScrollbackAndClear() {
  for (uint64_t refreshEvery : {uint64_t{0}, 37 * kFrame + 11}) {
    Fixture f;
    constexpr size_t lines = UartConsoleModel::MaximumLines + 137;
    f.engine.segments = {{5'000, std::vector<uint8_t>(lines, '\n')}};
    QSignalSpy removed(f.adapter.uart(), &QAbstractItemModel::rowsRemoved);
    const uint64_t end = 5'000 + (lines + 1) * kFrame;
    while (f.board.now() < end) {
      f.board.tick(refreshEvery ? std::min(refreshEvery, end - f.board.now())
                                : end - f.board.now());
      CHECK(f.adapter.refresh());
    }
    CHECK_EQ(f.uart().rowCount(), UartConsoleModel::MaximumLines);
    CHECK_EQ(f.uart().trimmedLines(), 137);
    CHECK_EQ(f.uart().txBytes(), qint64(lines));
    // Oldest rows are discarded first; the latest remain in order.
    CHECK(row(f.uart(), 0).cycle == number(stopSample(5'000 + 137 * kFrame)));
    CHECK(row(f.uart(), UartConsoleModel::MaximumLines - 1).cycle
          == number(stopSample(5'000 + (lines - 1) * kFrame)));
    // A single oversized batch never inserts rows it would immediately trim.
    CHECK_EQ(removed.isEmpty(), refreshEvery == 0);

    const auto boardBytes = f.board.uartTxBytes();
    const auto now = f.board.now();
    const auto steps = f.engine.steps;
    QSignalSpy countChanged(f.adapter.uart(), &UartConsoleModel::countChanged);
    CHECK(f.adapter.clearUart());
    CHECK_EQ(countChanged.size(), 1);
    CHECK_EQ(f.uart().rowCount(), 0);
    CHECK_EQ(f.uart().trimmedLines(), 0);
    CHECK_EQ(f.uart().txBytes(), qint64(lines));
    CHECK(f.board.uartTxBytes() == boardBytes);
    CHECK_EQ(f.board.now(), now);
    CHECK_EQ(f.engine.steps, steps);
    CHECK(f.adapter.clearUart());  // idempotent
    CHECK_EQ(countChanged.size(), 1);
  }

  // Bytes decoded before Clear stay cleared even if no refresh ran between
  // their decoding and the click (a running controller refreshes only every
  // ~16 ms of wall time).
  for (bool refreshFirst : {true, false}) {
    Fixture c;
    c.engine.segments = {{2'000, {'a', 'b', 'c', 'd'}}};
    c.runTo(2'000 + 2 * kFrame + kFrame / 2);  // 'a', 'b' decoded
    if (refreshFirst) CHECK(c.adapter.refresh());
    CHECK(c.adapter.clearUart());
    CHECK_EQ(c.uart().rowCount(), 0);
    CHECK_EQ(c.uart().txBytes(), 2);
    c.runTo(2'000 + 5 * kFrame);
    CHECK(c.adapter.refresh());
    CHECK_EQ(c.uart().rowCount(), 1);
    CHECK(row(c.uart(), 0).text == QStringLiteral("cd"));
    CHECK(row(c.uart(), 0).cycle == number(stopSample(2'000 + 2 * kFrame)));
  }

  // At the row limit an insert and a trim leave rowCount unchanged, but
  // countChanged still fires so views re-follow and trimmedLines updates.
  {
    Fixture cap;
    cap.engine.segments = {{5'000, std::vector<uint8_t>(UartConsoleModel::MaximumLines + 3, '\n')}};
    cap.runTo(5'000 + (UartConsoleModel::MaximumLines + 1) * kFrame);
    CHECK(cap.adapter.refresh());
    CHECK_EQ(cap.uart().rowCount(), UartConsoleModel::MaximumLines);
    const auto trimmed = cap.uart().trimmedLines();
    QSignalSpy counted(cap.adapter.uart(), &UartConsoleModel::countChanged);
    cap.runTo(5'000 + (UartConsoleModel::MaximumLines + 3) * kFrame);
    CHECK(cap.adapter.refresh());
    CHECK_EQ(cap.uart().rowCount(), UartConsoleModel::MaximumLines);
    CHECK(cap.uart().trimmedLines() > trimmed);
    CHECK(!counted.isEmpty());
  }

  // Listeners cannot re-enter while Clear notifies.
  {
    Fixture r;
    CHECK(r.adapter.sendUartText(QStringLiteral("abc")));
    int reentered = 0;
    const auto attempt = [&] {
      reentered += r.adapter.sendUartText(QStringLiteral("n")) + r.adapter.clearUart()
          + r.adapter.refresh() + r.adapter.setButton(0, true);
    };
    QObject::connect(r.adapter.uart(), &QAbstractItemModel::modelReset, r.adapter.uart(), attempt);
    QObject::connect(r.adapter.uart(), &UartConsoleModel::countChanged, r.adapter.uart(), attempt);
    CHECK(r.adapter.sendUartText(QStringLiteral("x")));  // countChanged during publication
    CHECK(r.adapter.clearUart());
    CHECK_EQ(reentered, 0);
    CHECK_EQ(r.uart().rxBytes(), 4);
    CHECK_EQ(r.uart().rowCount(), 0);
    CHECK(!r.board.buttonState(Button::C));
  }

  // Clearing mid-line: later bytes start a new row with their own stamp.
  Fixture f;
  f.engine.segments = {{2'000, {'a', 'b', 'c'}}};
  f.runTo(2'000 + 2 * kFrame + kFrame / 2);
  CHECK(f.adapter.refresh());
  CHECK(row(f.uart(), 0).text == QStringLiteral("ab"));
  CHECK(f.adapter.clearUart());
  f.runTo(2'000 + 4 * kFrame);
  CHECK(f.adapter.refresh());
  CHECK_EQ(f.uart().rowCount(), 1);
  CHECK(row(f.uart(), 0).text == QStringLiteral("c"));
  CHECK(row(f.uart(), 0).cycle == number(stopSample(2'000 + 2 * kFrame)));
}

void resetNotices() {
  Fixture f;
  const auto options = manualOptions();
  SimulationController controller(f.adapter, QStringLiteral("Serial"), options);
  f.engine.segments = {{1'000, {'x'}}, {300'000, {'y'}}};
  CHECK(f.adapter.sendUartText(QStringLiteral("rs")));
  // Advance around the controller so 'x' is decoded but not yet published,
  // and start the pulse so 'y' is decoded on a grid inside its 16 cycles.
  const uint64_t duringPulse = stopSample(300'000);
  f.runTo(duringPulse - 10);
  const uint64_t pulse = f.board.now();
  CHECK_EQ(f.uart().rowCount(), 1);
  CHECK(controller.reset());
  CHECK_EQ(f.board.now(), pulse + SimulationController::ResetCycles);
  CHECK(controller.step(500'000));
  const auto all = rows(f.uart());
  CHECK_EQ(all.size(), 4);
  CHECK_EQ(all[0].kind, int(UartConsoleModel::Rx));
  CHECK(all[1].text == QStringLiteral("x"));
  CHECK_EQ(all[2].kind, int(UartConsoleModel::Notice));
  CHECK(all[2].text == QStringLiteral("Reset: BTNC held for 16 cycles"));
  CHECK(all[2].cycle == number(pulse));
  CHECK(all[3].text == QStringLiteral("y"));
  CHECK(all[3].cycle == number(duringPulse));  // decoded mid-pulse, listed after it
  // The button pulse follows the notice's stamp exactly.
  CHECK(f.engine.buttonPokes.size() >= 2);
  CHECK(f.engine.buttonPokes[f.engine.buttonPokes.size() - 2]
        == std::make_pair(pulse, uint64_t{1}));
  CHECK(f.engine.buttonPokes.back() == std::make_pair(pulse + 16, uint64_t{0}));
  // Reset preserves the board's queued input; it keeps draining afterwards.
  CHECK_EQ(f.uart().rxBytes(), 2);
  CHECK_EQ(f.uart().pendingRxBytes(), 0);

  // A pulse that fails adds no notice; the controller reports the error.
  {
    Fixture failing;
    SimulationController broken(failing.adapter, QStringLiteral("Broken"), options);
    failing.engine.failSteps = true;
    CHECK(!broken.reset());
    CHECK(!broken.errorString().isEmpty());
    CHECK_EQ(failing.uart().rowCount(), 0);
    CHECK(!failing.board.buttonState(Button::C));
  }

  // Designs without UART pins get no terminal rows.
  Fixture none(Pins::None);
  SimulationController quiet(none.adapter, QStringLiteral("Quiet"), options);
  CHECK(quiet.reset());
  CHECK_EQ(none.uart().rowCount(), 0);
}

void threadAndReentrancy() {
  Fixture f;
  SimulationController controller(f.adapter, QStringLiteral("Nested"), manualOptions());
  bool accepted = true;
  std::thread worker([&] {
    accepted = f.adapter.sendUartText(QStringLiteral("t")) || f.adapter.clearUart();
  });
  worker.join();
  CHECK(!accepted);
  CHECK_EQ(f.uart().rxBytes(), 0);
  CHECK_EQ(f.uart().rowCount(), 0);

  CHECK(f.adapter.sendUartText(QStringLiteral("a")));
  const auto steps = f.engine.steps;
  bool nested = true;
  int calls = 0;
  const auto connection = QObject::connect(
      f.adapter.uart(), &QAbstractItemModel::rowsInserted, f.adapter.uart(), [&] {
        ++calls;
        // Includes the controller's reset path, whose notice would reenter.
        nested = f.adapter.sendUartText(QStringLiteral("n")) || f.adapter.clearUart()
            || f.adapter.refresh() || f.adapter.setButton(0, true) || controller.reset();
      });
  f.engine.segments = {{1'000, {'z'}}};
  f.runTo(2 * kFrame);
  CHECK(f.adapter.refresh());
  QObject::disconnect(connection);
  CHECK_EQ(calls, 1);
  CHECK(!nested);
  CHECK_EQ(f.uart().rowCount(), 2);
  CHECK_EQ(f.uart().rxBytes(), 1);
  CHECK(!f.board.buttonState(Button::C));
  // Refresh, send and clear never advance virtual time themselves.
  const auto now = f.board.now();
  const auto afterRun = f.engine.steps;
  CHECK(afterRun > steps);
  CHECK(f.adapter.refresh());
  CHECK(f.adapter.sendUartText(QStringLiteral("b")));
  CHECK(f.adapter.clearUart());
  CHECK_EQ(f.engine.steps, afterRun);
  CHECK_EQ(f.board.now(), now);
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  disconnectedAndAvailability();
  qmlExposure();
  outputRowsAndStamps();
  allBytesAndLongLines();
  inputQueueAndOrdering();
  boundedPendingQueue();
  framingErrorNotices();
  boundedScrollbackAndClear();
  resetNotices();
  threadAndReentrancy();
  std::puts("test_qt_uart_console: PASS");
  return 0;
}
