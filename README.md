# VirtualBasys

A virtual Basys 3 FPGA board for running Verilog designs without hardware.
Built with **C++20 and Verilator** for macOS; migration to **Qt 6 / Qt Quick** is in progress.

## Current progress

| Area | Status |
| --- | --- |
| Simulator | Working XDC pin bindings, switches, LEDs, buttons, seven-segment display, UART, VGA, VCD traces and regression tests. Counter, stopwatch, UART echo and VGA pattern examples are included. |
| Qt frontend — M0–M4 complete | Counter and stopwatch launchers, resizable workspace, board controls, Run/Pause/Step/Reset, exact virtual time and measured speed. Turbo and best-effort real-time pacing. |
| Next — M5 | UART terminal with send controls and scrollback. VGA, inspector and log views follow. |

**Qt currently runs the built-in counter and stopwatch**, each in its own launcher.
Arbitrary design loading and Qt peripheral/debug views are still pending.
The existing ImGui demos remain available for UART and VGA.

M4 verification: **30/30 full-suite checks**, **18/18 headless checks** and
**12/12 Qt sanitizer checks**, with two independent reviews.
[Verification and controls](docs/qt_control.md) · [Roadmap](docs/migration_plan.md)

## Screenshots

| Counter running in Qt | Stopwatch paused after simulation |
| --- | --- |
| ![Qt counter with simulation controls and measured speed](docs/screenshots/qt-counter-m4.png) | ![Qt stopwatch with simulation controls and virtual time](docs/screenshots/qt-stopwatch-m4.png) |

Captured from the real counter UI test and stopwatch benchmark using the same Qt board.

## Build and run

On macOS with Xcode Command Line Tools and Homebrew, run from the repository root:

```sh
brew install cmake verilator qtbase qtdeclarative
cmake -S . -B build/qt -DVB_BUILD_QT_GUI=ON -DVB_BUILD_GUI=OFF
cmake --build build/qt -j 8
./build/qt/src/qt/virtualbasys_qt
./build/qt/src/qt/virtualbasys_qt_stopwatch
```

Both start paused. Reset applies a 16-cycle board reset; virtual time continues.
Use `--preview` for the unloaded board or `--realtime` for best-effort 1× pacing.

Run tests with `ctest --test-dir build/qt --output-on-failure`.
The optional Surfer VCD check is skipped unless `surfer` is installed.
See the [build and testing guide](docs/qt_build.md) for frontend options and the
[board guide](docs/qt_board.md) for controls and screenshot reproduction.
