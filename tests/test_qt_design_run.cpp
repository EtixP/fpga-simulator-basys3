// virtualbasys run, as a user runs it: separate processes, offscreen. Oracle:
// the prebuilt example launchers, which link the same designs at build time,
// must write byte-identical logs for the same scripts. No design library or
// Verilator runtime is linked here.
//
// argv[1] = virtualbasys, argv[2..5] = the counter, stopwatch, uart_echo and
// vga_pattern launchers, argv[6] = repository root, argv[7] = a module built
// for another ABI (for the fake CMake, tests/data/fake_cmake.sh).
#include "check.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>

#include <csignal>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
QString app;
QString repo;
QTemporaryDir* scratch = nullptr;

struct Result {
  int exitCode = -1;
  QString errors;
  QString output;
};

using Environment = std::vector<std::pair<QString, QString>>;

void prepare(QProcess& process, const Environment& extra) {
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
  environment.insert(QStringLiteral("QT_QUICK_BACKEND"), QStringLiteral("software"));
  environment.insert(QStringLiteral("VB_DESIGN_CACHE"), scratch->filePath(QStringLiteral("cache")));
  for (const auto& [name, value] : extra) environment.insert(name, value);
  process.setProcessEnvironment(environment);
  process.setWorkingDirectory(repo);
}

Result launch(const QString& program, const QStringList& arguments, const Environment& extra = {}) {
  QProcess process;
  prepare(process, extra);
  process.start(program, arguments);
  if (!process.waitForFinished(290'000)) {
    process.kill();  // never leave a launcher behind
    process.waitForFinished(5'000);
    CHECK(!"the process did not exit");
  }
  Result result;
  result.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
  result.errors = QString::fromLocal8Bit(process.readAllStandardError());
  result.output = QString::fromLocal8Bit(process.readAllStandardOutput());
  return result;
}

// Starts a first compile, signals the app alone (as a terminal's Ctrl-C does:
// the build's own processes form another group) after `delayMs`, and checks
// that the build ends at once, stores nothing and leaves no compiler behind.
void interruptedBuild(const QString& name, int signal, int delayMs) {
  QProcess process;
  prepare(process, {});
  process.start(app, {"run", scratch->filePath(name), "--frames", "1"});
  CHECK(process.waitForStarted(10'000));
  QString errors;
  QElapsedTimer waited;
  waited.start();
  while (!errors.contains(QStringLiteral("virtualbasys: compiling")) && waited.elapsed() < 30'000) {
    process.waitForReadyRead(100);
    errors += QString::fromLocal8Bit(process.readAllStandardError());
  }
  CHECK(errors.contains(QStringLiteral("virtualbasys: compiling")));
  QThread::msleep(delayMs);
  const qint64 pid = process.processId();
  QElapsedTimer stopping;
  stopping.start();
  ::kill(static_cast<pid_t>(pid), signal);
  CHECK(process.waitForFinished(10'000));
  // Ended, not finished: a compile takes about 9 s, and one left to finish by
  // itself would take seconds more.
  std::fprintf(stderr, "%s stopped the build in %lld ms\n", qPrintable(name), stopping.elapsed());
  CHECK(stopping.elapsed() < 2'000);
  errors += QString::fromLocal8Bit(process.readAllStandardError());
  CHECK(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 130);
  CHECK(errors.endsWith(QStringLiteral("virtualbasys: interrupted\n")));
  if (name == QStringLiteral("slow3.v"))  // before the last line, "interrupted"
    CHECK(errors.contains(QStringLiteral("verilator: %Warning-WIDTHEXPAND")));
  QProcess pgrep;
  pgrep.start(QStringLiteral("/usr/bin/pgrep"), {"-f", QStringLiteral("virtualbasys-build-%1-").arg(pid)});
  CHECK(pgrep.waitForFinished(10'000));
  CHECK(pgrep.exitCode() == 1);  // nothing matched
  const QDir temp(QDir::tempPath());
  CHECK(temp.entryList({QStringLiteral("virtualbasys-*-%1-*").arg(pid)}, QDir::AllEntries | QDir::Hidden).isEmpty());
}

std::string readFile(const QString& path) {
  std::ifstream in(path.toStdString(), std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

QString file(const QString& name) { return scratch->filePath(name); }

void writeFile(const QString& name, const QByteArray& text) {
  QFile out(file(name));
  CHECK(out.open(QIODevice::WriteOnly));
  out.write(text);
}

// The same scripted run through virtualbasys run and a prebuilt launcher.
void sameAsPrebuilt(const char* name, const QString& launcher, const QString& design,
                    const QStringList& runScript, const QStringList& launcherScript) {
  const QString runLog = file(QStringLiteral("%1-run.log").arg(QLatin1String(name)));
  const QString launcherLog = file(QStringLiteral("%1-launcher.log").arg(QLatin1String(name)));
  const QString xdc = QStringLiteral("examples/%1.xdc").arg(design);
  const Result run = launch(app, QStringList{QStringLiteral("run"), QStringLiteral("examples/%1.v").arg(design),
                                             QStringLiteral("--xdc"), xdc}
                                     + runScript + QStringList{QStringLiteral("--log"), runLog});
  if (run.exitCode != 0) std::fprintf(stderr, "%s\n", qPrintable(run.errors));
  CHECK_EQ(run.exitCode, 0);
  const Result prebuilt = launch(launcher, launcherScript + QStringList{QStringLiteral("--log"), launcherLog});
  CHECK_EQ(prebuilt.exitCode, 0);
  const std::string expected = readFile(launcherLog);
  CHECK(expected.find("[cycle ") != std::string::npos);  // events, not just the header
  if (readFile(runLog) != expected) std::fprintf(stderr, "%s: logs differ\n", name);
  CHECK(readFile(runLog) == expected);
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication qapp(argc, argv);
  CHECK(argc == 8);
  app = QString::fromLocal8Bit(argv[1]);
  repo = QString::fromLocal8Bit(argv[6]);
  QTemporaryDir dir;
  CHECK(dir.isValid());
  scratch = &dir;
  // The fake CMake "builds" a copy of the foreign module in no time.
  const QString fakeCmake = file(QStringLiteral("fake_cmake.sh"));
  CHECK(QFile::copy(repo + QStringLiteral("/tests/data/fake_cmake.sh"), fakeCmake));
  CHECK(QFile::setPermissions(fakeCmake, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
  const Environment fake{{"VB_CMAKE", fakeCmake}, {"VB_FAKE_MODULE", QString::fromLocal8Bit(argv[7])}};

  // --- the four examples, built at run time, against the prebuilt launchers.
  const QStringList counter{"--frames", "3", "--switches", "0101", "--at", "150000:BTNU=1",
                            "--at", "250000:SW0=0"};
  sameAsPrebuilt("counter", QString::fromLocal8Bit(argv[2]), QStringLiteral("counter"), counter, counter);
  const QStringList stopwatch{"--frames", "300", "--at", "100000:BTNU=1", "--at", "1300000:BTNU=0",
                              "--at", "12500000:BTNL=1", "--at", "13700000:BTNL=0"};
  sameAsPrebuilt("stopwatch", QString::fromLocal8Bit(argv[3]), QStringLiteral("stopwatch"), stopwatch,
                 stopwatch);
  const QStringList uart{"--frames", "12", "--send", "100000:hello", "--send", "150000:A"};
  sameAsPrebuilt("uart", QString::fromLocal8Bit(argv[4]), QStringLiteral("uart_echo"), uart, uart);
  // A loaded design's frame is 100,000 cycles; the VGA launcher's 1,700,000:
  // 34 frames and 2 frames both end at cycle 3,400,016.
  sameAsPrebuilt("vga", QString::fromLocal8Bit(argv[5]), QStringLiteral("vga_pattern"),
                 {"--frames", "34", "--at", "3400016:BTNC=1"}, {"--frames", "2", "--at", "3400016:BTNC=1"});

  // The second run of the same sources reuses the module: no compile message.
  const Result again = launch(app, {"run", "examples/counter.v", "--xdc", "examples/counter.xdc",
                                    "--frames", "1", "--screenshot", file("again.png")});
  CHECK_EQ(again.exitCode, 0);
  CHECK(!again.errors.contains(QStringLiteral("compiling")));
  CHECK(!QImage(file("again.png")).isNull());

  // --- a design written for the Basys 3 master constraints: no --xdc.
  writeFile("mirror.v", "module mirror(input wire clk, input wire btnC, input wire [15:0] sw,\n"
                        "              output reg [15:0] led);\n"
                        "  always @(posedge clk) led <= btnC ? 16'h0 : sw;\n"
                        "endmodule\n");
  const Result mirror = launch(app, {"run", file("mirror.v"), "--frames", "1", "--switches", "1001",
                                     "--log", file("mirror.log")});
  CHECK_EQ(mirror.exitCode, 0);
  CHECK(mirror.errors.contains(QStringLiteral("virtualbasys: compiling 'mirror' with Verilator")));
  CHECK(!mirror.errors.contains(QStringLiteral("bind note:")));  // the unused board pins
  const std::string mirrorLog = readFile(file("mirror.log"));
  CHECK(mirrorLog.find("[cycle 1000] LED[0] 0->1") != std::string::npos);
  CHECK(mirrorLog.find("[cycle 1000] LED[3] 0->1") != std::string::npos);

  // --- the master clock is whatever port the constraints bind to W5.
  writeFile("ticker.v", "module ticker(input wire sysclk, output reg [15:0] led);\n"
                        "  always @(posedge sysclk) led <= led + 16'd1;\n"
                        "endmodule\n");
  // led counts every cycle: at the 1,000-cycle observation points its bit 9
  // (1000 = 0b1111101000) turns on first.
  writeFile("ticker.xdc", "set_property PACKAGE_PIN W5 [get_ports sysclk]\n"
                          "set_property PACKAGE_PIN V3 [get_ports {led[9]}]\n"
                          "set_property PACKAGE_PIN U16 [get_ports {typo[0]}]\n");
  const Result ticker = launch(app, {"run", file("ticker.v"), "--xdc", file("ticker.xdc"), "--frames", "1",
                                     "--log", file("ticker.log")});
  CHECK_EQ(ticker.exitCode, 0);
  CHECK(readFile(file("ticker.log")).find("[cycle 1000] LED[9] 0->1") != std::string::npos);
  // The user's own constraints: their mistakes are shown, with no hint about
  // the master file's names.
  CHECK(ticker.errors.contains(
      QStringLiteral("bind warning: constraint on port 'typo' which the design does not have — skipped")));
  CHECK(!ticker.errors.contains(QStringLiteral("hint:")));

  // --- several files, an include directory and a chosen top.
  QDir(dir.path()).mkpath(QStringLiteral("inc"));
  writeFile("inc/step.vh", "`define STEP 16'd3\n");
  writeFile("adder.v", "`include \"step.vh\"\n"
                       "module adder(input wire clk, output reg [15:0] led);\n"
                       "  always @(posedge clk) led <= led + `STEP;\n"
                       "endmodule\n");
  writeFile("wrapper.v", "module wrapper(input wire clk, output wire [15:0] led);\n"
                         "  adder u(.clk(clk), .led(led));\n"
                         "endmodule\n"
                         "module spare(input wire clk, output wire x); assign x = clk; endmodule\n");
  const Result tops = launch(app, {"run", file("wrapper.v"), file("adder.v"), "-I", file("inc"),
                                   "--frames", "0"});
  CHECK_EQ(tops.exitCode, 1);
  CHECK(tops.errors.contains(QStringLiteral(
      "error: the design has several top-level modules (wrapper, spare); choose one with --top NAME")));
  const Result chosen = launch(app, {"run", file("wrapper.v"), file("adder.v"), "-I" + file("inc"),
                                     "--top", "wrapper", "--frames", "1", "--log", file("wrapper.log")});
  CHECK_EQ(chosen.exitCode, 0);
  // 3 per cycle: 3000 = 0b101110111000 at cycle 1000.
  CHECK(readFile(file("wrapper.log")).find("[cycle 1000] LED[3] 0->1") != std::string::npos);

  // --- warnings are shown and the design runs; errors stop before a window.
  writeFile("widthy.v", "module widthy(input wire clk, input wire [15:0] sw, output reg [15:0] led);\n"
                        "  reg [7:0] count;\n"
                        "  always @(posedge clk) count <= count + sw[3:0];\n"
                        "  always @(*) led = count;\n"
                        "endmodule\n");
  const Result widthy = launch(app, {"run", file("widthy.v"), "--frames", "0"});
  CHECK_EQ(widthy.exitCode, 0);
  CHECK(widthy.errors.contains(QStringLiteral("verilator: %Warning-WIDTH")));
  writeFile("broken.v", "module broken(input wire clk, output reg led);\n"
                        "  always @(posedge clk) led <= ~led\nendmodule\n");
  const Result broken = launch(app, {"run", file("broken.v"), "--frames", "1", "--log", file("broken.log")});
  CHECK_EQ(broken.exitCode, 1);
  CHECK(broken.errors.contains(QStringLiteral("verilator: %Error: %1:3:1: syntax error").arg(file("broken.v"))));
  CHECK(broken.errors.contains(QStringLiteral("error: Verilator rejected the design")));
  CHECK(!QFile::exists(file("broken.log")));
  const Result missing = launch(app, {"run", file("nope.v")});
  CHECK_EQ(missing.exitCode, 1);
  CHECK(missing.errors.contains(QStringLiteral("error: cannot read source '%1'").arg(file("nope.v"))));
  const Result noSources = launch(app, {"run", "--frames", "1"});
  CHECK_EQ(noSources.exitCode, 1);
  CHECK(noSources.errors.contains(QStringLiteral("error: run: expected Verilog source files after 'run'")));
  // A design without the clock port is refused before it is compiled.
  const Result noClock = launch(app, {"run", file("ticker.v"), "--frames", "0"});  // no sysclk in the master file
  CHECK_EQ(noClock.exitCode, 1);
  CHECK(noClock.errors.contains(QStringLiteral(
      "error: 'ticker' has no input port 'clk' for the 100 MHz clock (pin W5); name its clock input clk, "
      "as the Basys 3 master constraints do, or bind it to W5 with --xdc")));
  writeFile("clockless.v", "module clockless(input wire [15:0] sw, output wire [15:0] led);\n"
                           "  assign led = sw;\nendmodule\n");
  writeFile("noclock.xdc", "set_property PACKAGE_PIN V17 [get_ports {sw[0]}]\n");
  const Result clockless = launch(app, {"run", file("clockless.v"), "--xdc", file("noclock.xdc")});
  CHECK_EQ(clockless.exitCode, 1);
  CHECK(clockless.errors.contains(QStringLiteral(
      "error: 'clockless' has no input port 'clk' for the 100 MHz clock (pin W5); bind its clock input to W5 in ")
      + file("noclock.xdc")));
  CHECK(!clockless.errors.contains(QStringLiteral("compiling")));
  const Result usage = launch(app, {});
  CHECK_EQ(usage.exitCode, 1);
  CHECK(usage.errors.contains(QStringLiteral("usage: virtualbasys run SOURCE.v")));
  // Launcher errors keep their messages behind run, and come before any
  // compile: the broken design is never even checked.
  const Result stray = launch(app, {"run", "examples/counter.v", "--frames", "1", "stray"});
  CHECK_EQ(stray.exitCode, 1);
  CHECK(stray.errors.contains(QStringLiteral("error: unknown or incomplete argument 'stray'")));
  const Result early = launch(app, {"run", file("broken.v"), "--frames", "x", "--bogus"});
  CHECK_EQ(early.exitCode, 1);
  CHECK(early.errors.contains(QStringLiteral("VirtualBasys: Unknown option 'bogus'.")));
  CHECK(!early.errors.contains(QStringLiteral("verilator:")));
  const Result earlyValue = launch(app, {"run", file("broken.v"), "--frames", "x"});
  CHECK(earlyValue.errors.contains(QStringLiteral("error: --frames 'x': expected a non-negative integer")));
  CHECK(!earlyValue.errors.contains(QStringLiteral("verilator:")));

  // run's own mistakes, all before any compile: the broken design is never
  // checked, so no verilator: lines appear.
  const auto refused = [&](const QStringList& arguments, const QString& message) {
    const Result result = launch(app, QStringList{"run"} + arguments);
    CHECK_EQ(result.exitCode, 1);
    if (!result.errors.contains(message)) std::fprintf(stderr, "%s\n", qPrintable(result.errors));
    CHECK(result.errors.contains(message));
    CHECK(!result.errors.contains(QStringLiteral("verilator:")));
  };
  refused({file("broken.v"), "--preview"},
          "error: --preview shows the board without a design and cannot be combined with run");
  refused({file("broken.v"), "--top", "a", "--top", "b"}, "error: --top is given more than once");
  refused({file("broken.v"), "--top", ""}, "error: --top needs a module name");
  refused({file("broken.v"), "--top"}, "error: --top needs a module name");
  refused({file("broken.v"), "--top=broken"}, "error: --top takes the module name as the next argument: --top NAME");
  refused({file("broken.v"), "-I", ""}, "error: -I needs a directory");
  refused({file("broken.v"), "--frames", "1", file("adder.v")},
          "error: source files go straight after 'run', before any option: '" + file("adder.v") + "'");
  refused({file("ticker.xdc")}, "error: '" + file("ticker.xdc") + "' is a constraints file; give it with --xdc");
  refused({file("broken.v"), "--xdc", file("nope.xdc")}, "error: cannot read XDC '" + file("nope.xdc") + "'");
  const Result xdcSource = launch(app, {"run", file("ticker.xdc")});
  CHECK(!xdcSource.errors.contains(QStringLiteral("expected Verilog source files")));

  // --help wherever it is given as an option, but not as another option's value.
  for (const QStringList& arguments : {QStringList{"--help"}, QStringList{"run", "x.v", "--top", "x", "--help"},
                                       QStringList{"run", "-I", "inc", "-h"}}) {
    const Result help = launch(app, arguments);
    CHECK_EQ(help.exitCode, 0);
    CHECK(help.output.contains(QStringLiteral("[options] run SOURCE.v [SOURCE.v ...] [--top NAME] [-I DIR]")));
    CHECK(help.output.contains(QStringLiteral("--top NAME (the top module")));
  }
  const Result notHelp = launch(app, {"run", file("broken.v"), "--at", "-h"});
  CHECK_EQ(notHelp.exitCode, 1);
  CHECK(!notHelp.output.contains(QStringLiteral("Usage:")));
  const Result preview = launch(app, {"--preview", "--smoke-test"});
  CHECK_EQ(preview.exitCode, 0);
  CHECK(preview.output.contains(QStringLiteral("Qt Quick smoke test: PASS")));

  // Tools that cannot run are named as ours, not as Verilator's messages.
  const Result noVerilator = launch(app, {"run", "examples/counter.v"}, {{"VB_VERILATOR_ROOT", "/nonexistent"}});
  CHECK_EQ(noVerilator.exitCode, 1);
  CHECK(noVerilator.errors.contains(QStringLiteral("error: cannot run Verilator (/nonexistent/bin/verilator)")));
  CHECK(!noVerilator.errors.contains(QStringLiteral("verilator:")));

  // A failed build shows the failing step's errors only.
  writeFile("fails.v", "module fails(input wire clk, output wire [15:0] led); assign led = 0; endmodule\n");
  Environment failing = fake;
  failing.emplace_back("VB_FAKE_FAIL", "build");
  const Result failed = launch(app, {"run", file("fails.v")}, failing);
  CHECK_EQ(failed.exitCode, 1);
  CHECK(failed.errors.contains(QStringLiteral("build: fake build failure\nerror: the design module failed to build\n")));
  CHECK(!failed.errors.contains(QStringLiteral("fake build progress")));

  // Late-stage warnings are shown; a cached module that does not load (here
  // the fake CMake's foreign one) is built again; ports the master
  // constraints do not name get a hint.
  writeFile("latchy.v", "module latchy(input wire clk, input wire [15:0] sw, output reg [15:0] leds);\n"
                        "  always @(*) if (sw[0]) leds = sw;\nendmodule\n");
  const Result foreign = launch(app, {"run", file("latchy.v"), "--frames", "0"}, fake);
  CHECK_EQ(foreign.exitCode, 1);
  CHECK(foreign.errors.contains(QStringLiteral("verilator: %Warning-LATCH: %1:2:3").arg(file("latchy.v"))));
  CHECK(foreign.errors.contains(QStringLiteral("error: the design module was built for a different VirtualBasys build")));
  const Result healed = launch(app, {"run", file("latchy.v"), "--frames", "0"});
  if (healed.exitCode != 0) std::fprintf(stderr, "%s\n", qPrintable(healed.errors));
  CHECK_EQ(healed.exitCode, 0);
  CHECK(healed.errors.contains(QStringLiteral("verilator: %Warning-LATCH")));
  CHECK(healed.errors.contains(QStringLiteral("virtualbasys: the cached module cannot be loaded (the design module "
                                              "was built for a different VirtualBasys build")));
  CHECK(healed.errors.contains(QStringLiteral("virtualbasys: compiling 'latchy' with Verilator again\n")));
  CHECK(healed.errors.contains(QStringLiteral("virtualbasys: compiled in ")));
  CHECK(healed.errors.contains(QStringLiteral("bind warning: design port 'leds' has no pin constraint in this XDC")));
  CHECK(healed.errors.contains(QStringLiteral("hint: without --xdc, ports need the Basys 3 master constraints' names")));

  // Ctrl-C (or SIGTERM, or SIGHUP) during a first compile ends it at once.
  writeFile("slow1.v", "module slow1(input wire clk, output reg [15:0] led); always @(posedge clk) led <= led + 1; endmodule\n");
  writeFile("slow2.v", "module slow2(input wire clk, output reg [15:0] led); always @(posedge clk) led <= led + 2; endmodule\n");
  interruptedBuild(QStringLiteral("slow1.v"), SIGINT, 3'000);  // while compiling
  interruptedBuild(QStringLiteral("slow2.v"), SIGTERM, 0);     // while configuring
  // Closing the terminal (SIGHUP) stops it too.
  // Its width warning is printed before the run is abandoned.
  writeFile("slow3.v", "module slow3(input wire clk, output reg [15:0] led); always @(posedge clk) led <= led + 3'd3; endmodule\n");
  interruptedBuild(QStringLiteral("slow3.v"), SIGHUP, 3'000);

  std::puts("test_qt_design_run: PASS");
  return 0;
}
