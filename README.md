# VirtualBasys

A virtual Basys 3 FPGA board for running Verilog designs without hardware.
Built with **C++20 and Verilator** for macOS; migration to **Qt 6 / Qt Quick** is in progress.

## Current progress

| Area | Status |
| --- | --- |
| Simulator | Working XDC pin bindings, switches, LEDs, buttons, seven-segment display, UART, VGA, VCD traces and regression tests. Counter, stopwatch, UART echo and VGA pattern examples are included. |
| Qt frontend — M0–M5 complete | Counter, stopwatch and UART echo launchers, resizable workspace, board controls, Run/Pause/Step/Reset, exact virtual time and measured speed. Turbo and best-effort real-time pacing. UART terminal with cycle-stamped TX/RX rows, send, hex view, clear and bounded scrollback. |
| Next — M6 | VGA output in Qt. Inspector and log views follow. |

**Qt runs the built-in counter, stopwatch and UART echo designs**, each in its own
launcher. Arbitrary design loading and the Qt VGA, inspector and log views are
still pending. The existing ImGui demos remain available for VGA.

M5 verification: **33/33 full-suite checks**, **18/18 headless checks** and
**15/15 Qt sanitizer checks**, with two independent reviews.
[UART terminal](docs/qt_uart.md) · [Simulation controls](docs/qt_control.md) ·
[Roadmap](docs/migration_plan.md)

## Screenshots

| UART echo in the Qt terminal | Reset during UART traffic |
| --- | --- |
| ![Qt UART terminal showing a cycle-stamped RX send and its TX echo](docs/screenshots/qt-uart-m5.png) | ![Qt UART terminal with a reset notice ordered between echoed bytes](docs/screenshots/qt-uart-reset-m5.png) |

Captured by the native UART UI test running the real `uart_echo` RTL. The second
image shows the design echoing unexpected bytes after a reset interrupts its
frames; the terminal shows exactly what the board decoded.

| Counter running in Qt | Stopwatch paused after simulation |
| --- | --- |
| ![Qt counter with simulation controls and measured speed](docs/screenshots/qt-counter-m4.png) | ![Qt stopwatch with simulation controls and virtual time](docs/screenshots/qt-stopwatch-m4.png) |

Captured in M4 from the real counter UI test and the stopwatch benchmark.

## Build and run

On macOS with Xcode Command Line Tools and Homebrew, run from the repository root:

```sh
brew install cmake verilator qtbase qtdeclarative
cmake -S . -B build/qt -DVB_BUILD_QT_GUI=ON -DVB_BUILD_GUI=OFF
cmake --build build/qt -j 8
./build/qt/src/qt/virtualbasys_qt
./build/qt/src/qt/virtualbasys_qt_stopwatch
./build/qt/src/qt/virtualbasys_qt_uart
```

All start paused. Reset applies a 16-cycle board reset; virtual time continues.
The UART launcher applies that reset once at startup, because the design needs
it before receiving (it opens at cycle 16). Type in the UART tab, press Return,
then Run or Step.
Use `--preview` for the unloaded board or `--realtime` for best-effort 1× pacing.

Run tests with `ctest --test-dir build/qt --output-on-failure`.
The optional Surfer VCD check is skipped unless `surfer` is installed.
See the [build and testing guide](docs/qt_build.md) for frontend options and the
[board guide](docs/qt_board.md) for controls and screenshot reproduction.
