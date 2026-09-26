// Scripted runs through the Qt controller. Oracle: the shared scheduler driven
// directly on a second board (the legacy demos' loop), so every advance path
// — Run batches of any size, Step and Reset — must apply script events at the
// same exact cycles and produce the same structured log.
#include VB_MODEL_HEADER
#include "board/BoardModel.h"
#include "check.h"
#include "constraints/Xdc.h"
#include "engine/VerilatorEngine.h"
#include "qt/BoardAdapter.h"
#include "qt/EventLogModel.h"
#include "qt/SimulationController.h"
#include "qt/UartConsoleModel.h"
#include "script/RunOptions.h"
#include "script/ScriptRunner.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QtQml/qqmlextensionplugin.h>

#include <cstdio>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

Q_IMPORT_QML_PLUGIN(VirtualBasys_BoardPlugin)

namespace {
using Controller = vb::qt::SimulationController;
std::string xdcText;

vb::RunOptions script(std::vector<std::string> arguments) {
  arguments.insert(arguments.begin(), {"script", "--log", "in-memory"});
  std::vector<char*> argv;
  for (auto& argument : arguments) argv.push_back(argument.data());
  vb::RunArgs run = vb::parseRunArgs(static_cast<int>(argv.size()), argv.data(), {});
  CHECK(run.errors.empty());
  return run.run;
}

struct Board {
  Board() : engine(vb::makeVerilatorEngine<VB_MODEL>({.topModule = VB_DESIGN})),
            board(*engine, vb::PinBinding::bind(vb::parseXdc(xdcText), *engine)) {}
  std::unique_ptr<vb::SimEngine> engine;
  vb::BoardModel board;
};

// A controller whose wall clock advances 20 ms per reading: every batch then
// publishes, i.e. the adapter refreshes after each batch.
struct Run : Board {
  explicit Run(uint64_t batchCycles, bool publishEveryBatch = false)
      : adapter(&board),
        controller(adapter, QStringLiteral("Script"), [&] {
          Controller::Options options;
          options.batchCycles = batchCycles;
          options.automaticScheduling = false;
          if (publishEveryBatch) options.nowNanoseconds = [this] { return wall += 20'000'000; };
          else options.nowNanoseconds = [] { return qint64(0); };
          return options;
        }()) {}
  // Runs until the controller stops; false if it never does.
  bool runToEnd(int maximumBatches = 5'000'000) {
    if (!controller.run()) return false;
    for (int batch = 0; batch < maximumBatches && controller.running(); ++batch)
      controller.processBatch();
    return !controller.running();
  }
  qint64 wall = 0;
  vb::qt::BoardAdapter adapter;
  Controller controller;
};

std::vector<std::string> legacyLog(const vb::RunOptions& options) {
  Board oracle;
  vb::ScriptCursor cursor;
  vb::initializeScriptedRun(oracle.board, options, cursor);
  for (long frame = 0; frame < options.maxFrames; ++frame)
    vb::advanceScripted(oracle.board, options, cursor, options.cyclesPerFrame);
  return oracle.board.structuredLog();
}

#if VB_COUNTER
// Script events land inside Step, inside the Reset pulse and across batches.
const std::vector<std::string> kInputs{
    "--frames", "5", "--switches", "0110", "--at", "120000:BTNU=1", "--at", "120001:BTNU=0",
    "--at", "212350:SW0=1", "--at", "212353:SW1=0", "--at", "333333:SW3=1",
    "--at", "400000:BTNC=1", "--at", "400020:BTNC=0", "--at", "499999:SW2=0"};

void batchSizesDoNotMatter() {
  const auto options = script(kInputs);
  const auto expected = legacyLog(options);
  CHECK(expected.size() > 40);
  for (uint64_t batch : {uint64_t{100'000}, uint64_t{997}, uint64_t{1}, uint64_t{1'000'000}}) {
    Run run(batch, batch == 997);
    QSignalSpy finished(&run.controller, &Controller::runFinished);
    CHECK(run.controller.startScript(options));
    CHECK(run.controller.scripted() && !run.controller.finished());
    CHECK_EQ(run.board.now(), 16);
    CHECK(run.controller.endCycleText() == QStringLiteral("500016"));
    CHECK(run.runToEnd());
    CHECK_EQ(run.board.now(), 500'016);  // 16 + 5 frames, never a batch more
    CHECK(run.controller.finished());
    CHECK_EQ(finished.size(), 1);
    CHECK(run.board.structuredLog() == expected);
    const auto [inputs, sends] = run.controller.unappliedScriptEvents();
    CHECK_EQ(inputs, 0);
    CHECK_EQ(sends, 0);
    // Nothing advances past the end.
    CHECK(!run.controller.run());
    CHECK(!run.controller.step(1));
    CHECK(!run.controller.reset());
    CHECK_EQ(run.board.now(), 500'016);
    // Later publications (Pause, a pacing change) never announce it again.
    CHECK(run.controller.pause());
    run.controller.setRealtime(true);
    CHECK_EQ(finished.size(), 1);
  }
}

// Step and Reset go through the same script path. The oracle replays the same
// commands with the shared scheduler: a Reset is BTNC held for 16 scripted cycles.
void stepAndResetApplyScripts() {
  const auto options = script(kInputs);
  Run run(100'000);
  Board oracle;
  vb::ScriptCursor cursor;
  vb::initializeScriptedRun(oracle.board, options, cursor);
  CHECK(run.controller.startScript(options));
  const auto step = [&](uint32_t cycles) {
    CHECK(run.controller.step(cycles));
    vb::advanceScripted(oracle.board, options, cursor, cycles);
  };
  const auto reset = [&] {
    CHECK(run.controller.reset());
    const bool held = oracle.board.buttonState(vb::Button::C);
    oracle.board.setButton(vb::Button::C, true);
    vb::advanceScripted(oracle.board, options, cursor, 16);
    oracle.board.setButton(vb::Button::C, held);
  };
  step(119'974);    // to 119990
  reset();          // 120000 and 120001 fall inside the pulse [119990, 120006)
  step(92'339);     // to 212345
  reset();          // 212350 and 212353 fall inside the pulse
  step(187'639);    // to 400000: the scripted BTNC press is applied at the end
  reset();          // keeps the scripted hold when the pulse ends at 400016
  step(10);         // 400020 releases it
  CHECK(run.board.structuredLog() == oracle.board.structuredLog());
  CHECK_EQ(run.board.now(), oracle.board.now());
  // Step is clipped at the end of the run.
  QSignalSpy finished(&run.controller, &Controller::runFinished);
  CHECK(run.controller.step(1'000'000));
  CHECK_EQ(run.board.now(), 500'016);
  CHECK(run.controller.finished());
  CHECK_EQ(finished.size(), 1);
  vb::advanceScripted(oracle.board, options, cursor, 500'016 - oracle.board.now());
  CHECK(run.board.structuredLog() == oracle.board.structuredLog());
}

// A scripted BTNC event during a Reset pulse owns BTNC afterwards, as during
// the startup reset; Reset restores the earlier level only without one.
void resetWithScriptedButton() {
  const auto buttonLines = [](const std::vector<std::string>& log) {
    std::vector<std::string> lines;
    for (const auto& line : log)
      if (line.find("] BTNC ") != std::string::npos) lines.push_back(line);
    return lines;
  };
  {
    // The script takes BTNC over during startup (cycle 5) and releases it
    // inside the later Reset pulse.
    Run run(100'000);
    CHECK(run.controller.startScript(script({"--frames", "5", "--switches", "1",
                                             "--at", "5:BTNC=1", "--at", "300003:BTNC=0"})));
    CHECK(run.controller.step(299'979));  // to 299995
    CHECK(run.controller.reset());         // pulse [299995, 300011)
    CHECK(!run.board.buttonState(vb::Button::C));
    CHECK(run.runToEnd());
    CHECK((buttonLines(run.board.structuredLog())
           == std::vector<std::string>{"[cycle 0] BTNC 0->1", "[cycle 300003] BTNC 1->0"}));
    CHECK(run.board.ledState(0) || run.board.ledState(1) || run.board.ledState(2));  // counting
  }
  {
    // The script presses BTNC inside the pulse: it stays pressed afterwards.
    Run run(100'000);
    CHECK(run.controller.startScript(script({"--frames", "5", "--switches", "1",
                                             "--at", "300003:BTNC=1"})));
    CHECK(run.controller.step(299'979));
    CHECK(run.controller.reset());
    CHECK(run.board.buttonState(vb::Button::C));
    CHECK(run.runToEnd());
    CHECK((buttonLines(run.board.structuredLog())
           == std::vector<std::string>{"[cycle 0] BTNC 0->1", "[cycle 16] BTNC 1->0",
                                       "[cycle 299995] BTNC 0->1"}));
  }
  {
    // Scripted BTNC events before the pulse do not count: Reset restores the
    // released button.
    Run run(100'000);
    CHECK(run.controller.startScript(script({"--frames", "5", "--switches", "1",
                                             "--at", "5:BTNC=1", "--at", "40:BTNC=0"})));
    CHECK(run.controller.step(299'979));
    CHECK(run.controller.reset());
    CHECK(!run.board.buttonState(vb::Button::C));
    CHECK(run.runToEnd());
    CHECK((buttonLines(run.board.structuredLog())
           == std::vector<std::string>{"[cycle 0] BTNC 0->1", "[cycle 40] BTNC 1->0",
                                       "[cycle 299995] BTNC 0->1", "[cycle 300011] BTNC 1->0"}));
  }
  {
    // A script that cannot start never runs, not even after a Reset.
    Run run(100'000);
    CHECK(!run.controller.startScript(script({"--frames", "999999999999999"})));
    CHECK(!run.controller.errorString().isEmpty());
    CHECK(!run.controller.reset());
    CHECK(!run.controller.run());
    CHECK(!run.controller.step(1));
  }
}

void zeroFramesAndUnlimitedRuns() {
  {
    // --frames 0: no reset, inputs or time, and the run ends immediately.
    Run run(100'000);
    QSignalSpy finished(&run.controller, &Controller::runFinished);
    CHECK(run.controller.startScript(script({"--frames", "0", "--switches", "1111"})));
    CHECK(run.controller.finished());
    CHECK_EQ(finished.size(), 1);
    CHECK_EQ(run.board.now(), 0);
    CHECK_EQ(run.board.structuredLog().size(), 6);
    CHECK(!run.board.switchState(0));
    CHECK(!run.controller.run());
    const auto [inputs, sends] = run.controller.unappliedScriptEvents();
    CHECK_EQ(inputs, 4);
    CHECK_EQ(sends, 0);
  }
  {
    // No --frames: the run continues until paused; scripts keep their cycles.
    auto options = script({"--switches", "0011", "--at", "250000:SW3=1", "--at", "9000000:SW0=0"});
    Run run(100'000);
    CHECK(run.controller.startScript(options));
    CHECK(run.controller.endCycleText().isEmpty());
    CHECK(run.controller.run());
    for (int batch = 0; batch < 7; ++batch) run.controller.processBatch();
    CHECK(run.controller.running() && !run.controller.finished());
    CHECK(run.controller.pause());
    CHECK_EQ(run.board.now(), 700'016);
    Board oracle;
    vb::ScriptCursor cursor;
    vb::initializeScriptedRun(oracle.board, options, cursor);
    vb::advanceScripted(oracle.board, options, cursor, 700'000);
    CHECK(run.board.structuredLog() == oracle.board.structuredLog());
    const auto [inputs, sends] = run.controller.unappliedScriptEvents();
    CHECK_EQ(inputs, 1);
  }
  {
    // A script starts once, on a fresh board.
    Run run(100'000);
    CHECK(run.controller.startScript(script({"--frames", "1"})));
    CHECK(!run.controller.startScript(script({"--frames", "1"})));
    Run late(100'000);
    CHECK(late.controller.step(5));
    CHECK(!late.controller.startScript(script({"--frames", "1"})));
    auto noFrames = script({"--frames", "1"});
    noFrames.cyclesPerFrame = 0;
    Run zero(100'000);
    CHECK(!zero.controller.startScript(noFrames));
  }
}

// --log keeps the whole board log for the file while the Logs view records,
// clears and stops: the view reads lines without removing any.
void retainedLogWithTheLogView() {
  const auto options = script(kInputs);
  const auto expected = legacyLog(options);
  Run run(100'000, true);
  CHECK(run.adapter.setBoardLogRetained(true));
  CHECK(run.controller.startScript(options));
  CHECK(run.controller.run());
  for (int batch = 0; batch < 2; ++batch) run.controller.processBatch();
  const std::size_t recordedFrom = run.board.structuredLog().size();
  CHECK(run.adapter.setLogRecording(true));
  CHECK(!run.adapter.setBoardLogRetained(false));  // not while recording
  for (int batch = 0; batch < 2; ++batch) run.controller.processBatch();
  // The view starts at the lines logged after Record, without clearing any.
  {
    const auto& log = *run.adapter.eventLog();
    std::size_t line = recordedFrom;
    for (int row = 0; row < log.rowCount(); ++row) {
      const auto index = log.index(row, 0);
      if (index.data(vb::qt::EventLogModel::KindRole).toInt() == vb::qt::EventLogModel::Notice) continue;
      CHECK(line < run.board.structuredLog().size());
      CHECK(("[cycle " + index.data(vb::qt::EventLogModel::CycleRole).toString().toStdString() + "] "
             + index.data(vb::qt::EventLogModel::TextRole).toString().toStdString())
            == run.board.structuredLog()[line++]);
    }
    CHECK_EQ(line, run.board.structuredLog().size());
    CHECK(line > recordedFrom);
  }
  CHECK(run.adapter.clearEventLog());
  const std::size_t clearedAt = run.board.structuredLog().size();
  CHECK(run.runToEnd());
  CHECK(run.adapter.setLogRecording(false));
  CHECK(run.board.structuredLog() == expected);  // nothing drained away
  CHECK(run.board.logEnabled());
  // The view holds exactly the lines logged after Clear, then the stop notice.
  const auto& log = *run.adapter.eventLog();
  std::vector<std::string> shown;
  for (int row = 0; row < log.rowCount(); ++row) {
    const auto index = log.index(row, 0);
    if (index.data(vb::qt::EventLogModel::KindRole).toInt() == vb::qt::EventLogModel::Notice) continue;
    shown.push_back("[cycle " + index.data(vb::qt::EventLogModel::CycleRole).toString().toStdString()
                    + "] " + index.data(vb::qt::EventLogModel::TextRole).toString().toStdString());
  }
  CHECK(shown == std::vector<std::string>(expected.begin() + static_cast<std::ptrdiff_t>(clearedAt),
                                          expected.end()));
  CHECK(!shown.empty());
}
#endif

#if VB_UART_ECHO
struct Row {
  int kind;
  std::string text;
  uint64_t cycle;
  bool operator==(const Row&) const = default;
};

std::vector<Row> rows(const vb::qt::UartConsoleModel& console) {
  std::vector<Row> result;
  for (int row = 0; row < console.rowCount(); ++row) {
    const auto index = console.index(row, 0);
    result.push_back({index.data(vb::qt::UartConsoleModel::KindRole).toInt(),
                      index.data(vb::qt::UartConsoleModel::TextRole).toString().toStdString(),
                      index.data(vb::qt::UartConsoleModel::CycleRole).toString().toULongLong()});
  }
  return result;
}

// Scripted sends are terminal RX rows stamped with their first start bit and
// placed, like typed sends, at the cycle they were sent: after every echo
// stamped at or before it, however often the adapter refreshes.
void scriptedSendsInTheTerminal() {
  const auto options = script({"--frames", "12", "--send", "100000:hello", "--send", "150000:A"});
  {
    // Scripted bytes count as queued until their frames end.
    Run run(100'000);
    CHECK(run.controller.startScript(options));
    CHECK(run.controller.step(159'984));  // to 160000: all six frames still queued
    CHECK_EQ(run.adapter.uart()->pendingRxBytes(), 6);
    CHECK(run.runToEnd());
    CHECK_EQ(run.adapter.uart()->pendingRxBytes(), 0);
  }
  {
    // A scripted send is not limited like typed text; the terminal then
    // explains a typed send's rejection without negative free space.
    Run run(100'000);
    CHECK(run.controller.startScript(script({"--send", "100:" + std::string(5000, 'x')})));
    CHECK(run.controller.step(100));
    CHECK_EQ(run.adapter.uart()->pendingRxBytes(), 5000);
    CHECK(!run.adapter.sendUartText(QStringLiteral("hi")));
    CHECK(run.adapter.uart()->sendError().contains(QStringLiteral("only 0 of 4096 B are free")));
  }
  std::vector<Row> reference;
  for (const bool publishEveryBatch : {true, false}) {
    Run run(publishEveryBatch ? 997 : 1'000'000, publishEveryBatch);
    CHECK(run.controller.startScript(options));
    CHECK(run.runToEnd());
    CHECK(run.board.structuredLog() == legacyLog(options));
    const auto& console = *run.adapter.uart();
    CHECK_EQ(console.rxBytes(), 6);
    CHECK_EQ(console.txBytes(), 6);
    const auto shown = rows(console);
    // Both sends (cycles 100000 and 150000) precede the first echo.
    CHECK(shown.size() >= 3);
    CHECK(shown[0] == (Row{vb::qt::UartConsoleModel::Rx, "hello", 100'000}));
    // Sent at 150000, queued behind hello's five 10-bit frames.
    CHECK(shown[1] == (Row{vb::qt::UartConsoleModel::Rx, "A", 100'000 + 5 * 10 * vb::kUartCyclesPerBit}));
    std::string tx;
    uint64_t last = 0;
    for (std::size_t i = 2; i < shown.size(); ++i) {
      CHECK(shown[i].kind == vb::qt::UartConsoleModel::Tx);
      CHECK(shown[i].cycle > 150'000 && shown[i].cycle >= last);
      last = shown[i].cycle;
      tx += shown[i].text;
    }
    CHECK(tx == "helloA");
    if (reference.empty())
      reference = shown;
    else
      CHECK(shown == reference);  // independent of refresh frequency
  }
  // Clear drops scripted rows not yet shown, like decoded traffic. With a
  // fixed clock, batches never publish: both sends are pending at Clear.
  Run run(100'000);
  CHECK(run.controller.startScript(options));
  CHECK(run.controller.run());
  run.controller.processBatch();
  run.controller.processBatch();  // to 200016: both sends queued, unshown
  CHECK(run.adapter.clearUart());
  CHECK(run.runToEnd());
  std::string tx;
  for (const auto& row : rows(*run.adapter.uart())) {
    CHECK(row.kind != vb::qt::UartConsoleModel::Rx);
    tx += row.text;
  }
  CHECK(tx == "helloA");  // every echo was decoded after Clear

  // A Reset cut short by the end of a run reports the cycles it held.
  Run shortRun(100'000);
  CHECK(shortRun.controller.startScript(script({"--frames", "1"})));
  CHECK(shortRun.controller.step(99'995));  // 5 cycles before the end
  CHECK(shortRun.controller.reset());
  CHECK(shortRun.controller.finished());
  bool noticed = false;
  for (const auto& row : rows(*shortRun.adapter.uart()))
    noticed = noticed || row.text == "Reset: BTNC held for 5 cycles";
  CHECK(noticed);
}
#endif
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  CHECK(argc == 2);
  std::ifstream in(argv[1], std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  xdcText = text.str();
  CHECK(!xdcText.empty());
#if VB_COUNTER
  batchSizesDoNotMatter();
  stepAndResetApplyScripts();
  resetWithScriptedButton();
  zeroFramesAndUnlimitedRuns();
  retainedLogWithTheLogView();
#endif
#if VB_UART_ECHO
  scriptedSendsInTheTerminal();
#endif
  std::printf("test_qt_script_run (%s): PASS\n", VB_DESIGN);
  return 0;
}
