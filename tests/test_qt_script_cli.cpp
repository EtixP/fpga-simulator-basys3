// Scripted Qt launcher runs against the legacy scripted-run loop. The launcher
// runs offscreen as a separate process; the oracle replays the same arguments
// in-process exactly as the legacy demos did: the shared startup, then one
// scripted advance of cyclesPerFrame cycles per frame. The log files must be
// byte-identical, whatever batches the Qt controller advanced in.
//
// argv[1] = the launcher executable, argv[2] = its design's example XDC.
#include VB_MODEL_HEADER
#include "board/BoardModel.h"
#include "check.h"
#include "constraints/Xdc.h"
#include "engine/VerilatorEngine.h"
#include "script/RunOptions.h"
#include "script/ScriptRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QProcess>
#include <QThread>
#include <QTemporaryDir>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
std::string launcher;
std::string exampleXdc;

std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

// The legacy demos' loop (GuiApp.mm): startup, then maxFrames advances of one
// frame each. Returns the structured log as the launcher writes it.
std::string legacyLog(const std::vector<std::string>& arguments) {
  // --log turns logging on at startup; the oracle keeps the lines in memory.
  std::vector<std::string> owned{"legacy", "--log", "in-memory"};
  owned.insert(owned.end(), arguments.begin(), arguments.end());
  std::vector<char*> argv;
  for (auto& argument : owned) argv.push_back(argument.data());
  vb::RunArgs run = vb::parseRunArgs(static_cast<int>(argv.size()), argv.data(), exampleXdc);
  CHECK(run.errors.empty());
  run.run.cyclesPerFrame = VB_CYCLES_PER_FRAME;
  auto engine = vb::makeVerilatorEngine<VB_MODEL>({.topModule = VB_DESIGN});
  vb::BoardModel board(*engine, vb::PinBinding::bind(vb::parseXdc(readFile(run.xdcPath)), *engine));
  vb::ScriptCursor cursor;
  vb::initializeScriptedRun(board, run.run, cursor);
  for (long frame = 0; frame < run.run.maxFrames; ++frame)
    vb::advanceScripted(board, run.run, cursor, run.run.cyclesPerFrame);
  std::string log;
  for (const auto& line : board.structuredLog()) log += line + '\n';
  return log;
}

struct Result {
  int exitCode = -1;
  QString errors;
};

Result launch(const std::vector<std::string>& arguments) {
  QProcess process;
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
  environment.insert(QStringLiteral("QT_QUICK_BACKEND"), QStringLiteral("software"));
  process.setProcessEnvironment(environment);
  QStringList list;
  for (const auto& argument : arguments) list << QString::fromStdString(argument);
  process.start(QString::fromStdString(launcher), list);
  if (!process.waitForFinished(170'000)) {
    process.kill();  // never leave a hung launcher behind
    process.waitForFinished(5'000);
    CHECK(!"the launcher did not exit");
  }
  Result result;
  result.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
  result.errors = QString::fromLocal8Bit(process.readAllStandardError());
  return result;
}

// Runs the launcher with the script plus --log (and --screenshot) and compares
// its log with the legacy loop's. Returns the launcher's stderr.
QString checkRun(const QTemporaryDir& dir, const char* name, std::vector<std::string> script,
                 bool screenshot = true) {
  const std::string log = QDir(dir.path()).filePath(QStringLiteral("%1.log").arg(name)).toStdString();
  const std::string image = QDir(dir.path()).filePath(QStringLiteral("%1.png").arg(name)).toStdString();
  std::vector<std::string> arguments = script;
  arguments.insert(arguments.end(), {"--log", log});
  if (screenshot) arguments.insert(arguments.end(), {"--screenshot", image});
  const Result result = launch(arguments);
  if (result.exitCode != 0) std::fprintf(stderr, "%s\n", qPrintable(result.errors));
  CHECK_EQ(result.exitCode, 0);
  const std::string actual = readFile(log);
  const std::string expected = legacyLog(script);
  if (actual != expected)
    std::fprintf(stderr, "%s: log differs (%zu vs %zu bytes)\n", name, actual.size(), expected.size());
  CHECK(actual == expected);
  if (screenshot) {
    const QImage shot(QString::fromStdString(image));
    CHECK(!shot.isNull() && shot.width() >= 960 && shot.height() >= 640);
    CHECK(QImageReader(QString::fromStdString(image)).format() == "png");  // by suffix
  }
  return result.errors;
}

int lines(const std::string& text) {
  int count = 0;
  for (const char character : text) count += character == '\n';
  return count;
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  CHECK(argc == 3);
  launcher = argv[1];
  exampleXdc = argv[2];
  QTemporaryDir dir;
  CHECK(dir.isValid());
  const std::string design = VB_DESIGN;

  if (design == "counter") {
    // Switch presets, a button hold, a late switch change, and one event past
    // the end, which is reported and never applied.
    const QString errors = checkRun(dir, "counter", {"--frames", "3", "--switches", "0101",
        "--at", "150000:BTNU=1", "--at", "250000:SW0=0", "--at", "99999999:SW3=1"});
    CHECK(errors.contains(QStringLiteral(
        "warning: 1 scripted --at event(s) beyond the run horizon were never applied")));
    CHECK(lines(legacyLog({"--frames", "3", "--switches", "0101"})) > 20);
    // A scripted BTNC hold takes over the startup reset and outlasts it.
    checkRun(dir, "counter-reset", {"--frames", "2", "--at", "5:BTNC=1", "--at", "40:BTNC=0",
                                    "--switches", "1"});
    // Same-cycle inputs apply in argument order, --switches included: SW0 is
    // cleared then set at cycle 0, SW1 set then cleared at 500.
    const std::vector<std::string> order{"--frames", "1", "--at", "0:SW0=0", "--switches", "1",
                                         "--at", "500:SW1=1", "--at", "500:SW1=0"};
    const std::string ordered = legacyLog(order);
    CHECK(ordered.find("[cycle 0] SW0 0->1") != std::string::npos);
    CHECK(ordered.find("[cycle 0] SW0 1->0") == std::string::npos);  // not set then cleared
    CHECK(ordered.find("[cycle 500] SW1 1->0") != std::string::npos);
    checkRun(dir, "counter-order", order, false);
    // And the other way round: --switches first, then the clearing --at.
    const std::vector<std::string> reversed{"--frames", "1", "--switches", "1", "--at", "0:SW0=0"};
    CHECK(legacyLog(reversed).find("[cycle 0] SW0 1->0") != std::string::npos);
    checkRun(dir, "counter-reversed", reversed, false);
    // Zero frames: no reset, no inputs, no time; the log is its header.
    checkRun(dir, "counter-zero", {"--frames", "0", "--switches", "1111"}, false);
    CHECK_EQ(lines(legacyLog({"--frames", "0"})), 6);
    // Other constraints: the swapped binding moves LEDs and switches.
    const std::string swapped = std::string(VB_SOURCE_DIR) + "/tests/data/counter_swapped.xdc";
    checkRun(dir, "counter-xdc", {"--xdc", swapped, "--frames", "2", "--switches", "0011"}, false);
    // Argument errors use the shared messages and exit 1 without a window.
    const Result bad = launch({"--frames", "-7", "--at", "10:SW99=1", "--send", "nocolon"});
    CHECK_EQ(bad.exitCode, 1);
    CHECK(bad.errors.contains(QStringLiteral("error: --frames '-7': expected a non-negative integer")));
    CHECK(bad.errors.contains(QStringLiteral("error: --at '10:SW99=1': unknown input 'SW99'")));
    CHECK(bad.errors.contains(QStringLiteral("error: --send 'nocolon': expected CYCLE:TEXT")));
    const Result missing = launch({"--xdc", "/nonexistent/x.xdc"});
    CHECK_EQ(missing.exitCode, 1);
    CHECK(missing.errors.contains(QStringLiteral("error: cannot read XDC '/nonexistent/x.xdc'")));
    const Result empty = launch({"--xdc", ""});  // e.g. an unset shell variable
    CHECK_EQ(empty.exitCode, 1);
    CHECK(empty.errors.contains(QStringLiteral("error: cannot read XDC ''")));
    // Constraint diagnostics pass through, prefixed as in the legacy demos.
    const QString odd = QDir(dir.path()).filePath(QStringLiteral("odd.xdc"));
    {
      QFile file(odd);
      CHECK(file.open(QIODevice::WriteOnly));
      file.write(QByteArray::fromStdString(readFile(exampleXdc)) + "\nset_false_path -from x\n"
                 "set_property PACKAGE_PIN T1 [get_ports nosuch]\n");
    }
    const Result diagnostics = launch({"--xdc", odd.toStdString(), "--frames", "0"});
    CHECK_EQ(diagnostics.exitCode, 0);
    const auto hasLine = [&](const char* prefix) {
      const QString start = QString::fromLatin1(prefix);
      return diagnostics.errors.startsWith(start) || diagnostics.errors.contains(QLatin1Char('\n') + start);
    };
    CHECK(hasLine("xdc "));
    CHECK(hasLine("bind "));
    CHECK(diagnostics.errors.contains(QStringLiteral("nosuch")));
    const Result previewXdc = launch({"--preview", "--xdc", exampleXdc});
    CHECK_EQ(previewXdc.exitCode, 1);
    CHECK(previewXdc.errors.contains(QStringLiteral("--preview shows no design")));
    // --xdc alone keeps the launch interactive; --smoke-test needs that.
    CHECK_EQ(launch({"--xdc", exampleXdc, "--smoke-test"}).exitCode, 0);
    const Result smoke = launch({"--smoke-test", "--frames", "1"});
    CHECK_EQ(smoke.exitCode, 1);
    CHECK(smoke.errors.contains(QStringLiteral("error: --smoke-test cannot be combined with a scripted run")));
    const Result badShot = launch({"--frames", "1", "--screenshot", "/nonexistent/dir/x.png"});
    CHECK_EQ(badShot.exitCode, 1);
    CHECK(badShot.errors.contains(QStringLiteral("error: could not write screenshot '/nonexistent/dir/x.png'")));
    const QString weird = QDir(dir.path()).filePath(QStringLiteral("shot.weird"));
    const Result weirdShot = launch({"--frames", "1", "--screenshot", weird.toStdString()});
    CHECK_EQ(weirdShot.exitCode, 0);
    CHECK(weirdShot.errors.contains(QStringLiteral("warning: Qt cannot write '.weird' images; the screenshot is BMP")));
    CHECK(QImageReader(weird).format() == "bmp");
    // Ctrl-C or SIGTERM ends a run normally: its log is written, and an
    // interrupted --frames run says it saved no screenshot.
    const auto interrupted = [&](std::vector<std::string> arguments) {
      QProcess process;
      QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
      environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
      process.setProcessEnvironment(environment);
      QStringList list;
      for (const auto& argument : arguments) list << QString::fromStdString(argument);
      process.start(QString::fromStdString(launcher), list);
      CHECK(process.waitForStarted(10'000));
      QThread::msleep(1500);
      process.terminate();
      if (!process.waitForFinished(30'000)) {
        process.kill();  // never leave a launcher behind
        process.waitForFinished(5'000);
        CHECK(!"the launcher ignored SIGTERM");
      }
      CHECK(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0);
      return QString::fromLocal8Bit(process.readAllStandardError());
    };
    {
      const std::string log = QDir(dir.path()).filePath(QStringLiteral("unlimited.log")).toStdString();
      const std::string shot = QDir(dir.path()).filePath(QStringLiteral("unlimited.png")).toStdString();
      const QString errors = interrupted({"--switches", "1", "--log", log, "--screenshot", shot});
      CHECK(errors.contains(QStringLiteral("warning: --screenshot is written only when --frames ends the run")));
      const std::string written = readFile(log);
      CHECK(written.find("[cycle 0] BTNC 0->1") != std::string::npos);
      CHECK(written.find("[cycle 16] BTNC 1->0") != std::string::npos);
      CHECK(written.find("[cycle 0] SW0 0->1") != std::string::npos);
      CHECK(!QFile::exists(QString::fromStdString(shot)));
    }
    {
      const std::string log = QDir(dir.path()).filePath(QStringLiteral("long.log")).toStdString();
      const std::string shot = QDir(dir.path()).filePath(QStringLiteral("long.png")).toStdString();
      const QString errors = interrupted({"--frames", "1000000", "--log", log, "--screenshot", shot});
      CHECK(errors.contains(QStringLiteral("warning: the run ended early; no screenshot was saved")));
      CHECK(readFile(log).find("[cycle 16] BTNC 1->0") != std::string::npos);
      CHECK(!QFile::exists(QString::fromStdString(shot)));
    }
    const Result preview = launch({"--preview", "--frames", "1"});
    CHECK_EQ(preview.exitCode, 1);
    // Nothing is silently ignored: stray words and --opt=value are errors.
    const Result stray = launch({"--frames", "1", "--send", "100:hello", "world"});
    CHECK_EQ(stray.exitCode, 1);
    CHECK(stray.errors.contains(QStringLiteral("error: unknown or incomplete argument 'world'")));
    // Errors come in argument order, "--" included, as in the legacy demos.
    const Result mixed = launch({"one", "--frames", "x", "--", "two"});
    CHECK_EQ(mixed.exitCode, 1);
    const auto at = [&](const char* message) { return mixed.errors.indexOf(QString::fromLatin1(message)); };
    CHECK(at("error: unknown or incomplete argument 'one'") >= 0);
    CHECK(at("error: unknown or incomplete argument 'one'") < at("error: --frames 'x'"));
    CHECK(at("error: --frames 'x'") < at("error: unknown or incomplete argument '--'"));
    CHECK(at("error: unknown or incomplete argument '--'") < at("error: unknown or incomplete argument 'two'"));
    const Result inlineValue = launch({"--frames=1"});
    CHECK_EQ(inlineValue.exitCode, 1);
    CHECK(inlineValue.errors.contains(QStringLiteral("error: unknown or incomplete argument '--frames=1'")));
    // --frames 0 is a launch check: no screenshot, even to an unwritable path.
    const QString zeroShot = QDir(dir.path()).filePath(QStringLiteral("zero.png"));
    CHECK_EQ(launch({"--frames", "0", "--screenshot", zeroShot.toStdString()}).exitCode, 0);
    CHECK(!QFile::exists(zeroShot));
    CHECK_EQ(launch({"--frames", "0", "--screenshot", "/nonexistent/dir/x.png"}).exitCode, 0);
    const Result unwritable = launch({"--frames", "1", "--log", "/nonexistent/dir/x.log"});
    CHECK_EQ(unwritable.exitCode, 1);
    CHECK(unwritable.errors.contains(QStringLiteral("error: could not write log '/nonexistent/dir/x.log'")));
  } else if (design == "stopwatch") {
    // 0.3 s: start, a lap and a stop, each held longer than the design's
    // debounce, while the display counts.
    const std::vector<std::string> presses{
        "--frames", "300", "--at", "100000:BTNU=1", "--at", "1300000:BTNU=0",
        "--at", "12500000:BTNL=1", "--at", "13700000:BTNL=0",
        "--at", "20000000:BTNU=1", "--at", "21200000:BTNU=0"};
    const std::string expected = legacyLog(presses);
    CHECK(expected.find("SSEG[0] '8'->'9'") != std::string::npos);
    checkRun(dir, "stopwatch", presses);
  } else if (design == "uart_echo") {
    // Two sends, one queued behind the other; the design echoes every byte.
    const std::string expected = legacyLog({"--frames", "12", "--send", "100000:hello",
                                            "--send", "150000:A"});
    CHECK(expected.find("UART TX 0x41 'A'") != std::string::npos);
    checkRun(dir, "uart_echo", {"--frames", "12", "--send", "100000:hello", "--send", "150000:A"});
  } else if (design == "vga_pattern") {
    // Two VGA-length frames, pinned by events at the last cycle (applied) and
    // one after it (reported); the monitor's frame is in the screenshot.
    const QString errors = checkRun(dir, "vga_pattern", {"--frames", "2", "--at", "3400016:BTNC=1",
                                                         "--at", "3400017:BTNC=0"});
    CHECK(legacyLog({"--frames", "2", "--at", "3400016:BTNC=1"}).find("[cycle 3400016] BTNC 0->1")
          != std::string::npos);
    CHECK(errors.contains(QStringLiteral(
        "warning: 1 scripted --at event(s) beyond the run horizon were never applied")));
  }
  std::printf("test_qt_script_cli (%s): PASS\n", VB_DESIGN);
  return 0;
}
