# Qt simulation controls

M4 runs the built-in counter and stopwatch designs in the Qt board. Both start
**paused at cycle zero**. Use `virtualbasys_qt` for the counter and
`virtualbasys_qt_stopwatch` for the stopwatch; each executable links one Verilated
design and embeds its XDC. Arbitrary design compilation and switching designs
inside a running window remain future work. `--preview` opens an unloaded board.

## Controls and timing

| Control | Behavior |
| --- | --- |
| Run / Pause | Run fixed 100,000-cycle batches; Pause cancels future batches and advances no cycles. |
| Step | Advance exactly the selected 1–1,000,000 cycles while paused; default is one cycle. |
| Reset | Pause, assert BTNC for exactly 16 cycles, then restore its previous level. |
| Turbo | Run continuously, yielding at least 1 ms between batches for GUI event delivery. |
| 1× real-time target | Add pacing waits when simulation is ahead of the 100 MHz virtual clock; retain only the common GUI yield when behind. |

Reset is a physical board pulse. Virtual time stays monotonic; switches, other
held buttons, logs, queued UART input and display persistence are preserved.
An already-held BTNC remains held until its owner releases it. Reset digits become
visible as the RTL multiplexer scans them; the frontend does not clear the display
cache artificially. Reset is unavailable if the design does not bind BTNC.

For the counter, turn on SW0–SW3 and Run or Step to increment the LEDs. For the
stopwatch, Run and hold BTNU long enough for the RTL debounce to observe 1 million
stable cycles. At less than 1× simulation speed, that takes longer in wall time.

Each cycle is 10 ns. Cycle and virtual-time labels are formatted from integers in
C++, including above JavaScript's exact-number range. Display reads, layout and
pacing changes advance no time. Wall time decides when a batch runs, never its
cycle budget or the timing of peripheral decoding. The controller rejects advances
beyond the last representable 1,000-cycle observation boundary rather than allowing
the board's next-grid calculation to wrap.

The speed display is measured cycles divided by actual elapsed monotonic wall time,
including event-loop, rendering and pacing delays. Measurements cover at least
250 ms; snapshots publish at roughly 16 ms intervals. The multiplier divides that
rate by 100 million cycles/second. Run and pacing changes start fresh measurement
windows. Paused/stepping states show no current throughput; Run initially shows
“Measuring speed…”. A 1× target is best effort, not a performance guarantee.

## Ownership and scheduling

The C++ composition root owns engine → BoardModel → BoardAdapter →
SimulationController → QML engine and destroys them in reverse order. Board and
adapter identity stay fixed throughout the window's lifetime. QML sees the adapter
and controller, never an engine, binding or signal handle.

The controller uses only BoardModel's existing inputs, `tick()` and `now()`, plus
the adapter's snapshot refresh. No backend reset API or engine reconstruction is
needed. All access stays on the GUI thread; no worker thread or concurrent board
access is introduced. A single-shot precise Qt timer queues one bounded batch per
turn, with a minimum 1 ms yield in both modes. This avoids continuously rearming
a zero timer, which can interfere with GUI event delivery; see the
[Qt timer guidance](https://doc.qt.io/qt-6/qtimer.html#interval-prop).
The callback rechecks Run and thread ownership; nested model publication
defers the callback without losing the next timer. Synchronous reentrant controller
commands and stepping during adapter publication are rejected.

Qt widgets retain the [board input ownership contract](qt_board.md). Runtime
simulation errors pause execution and appear in the toolbar; Reset can recover
when the design permits. Pausing and teardown cannot leave queued stepping active.

## Verification

```sh
ctest --test-dir build/qt -R '^qt_simulation_' --output-on-failure
cmake --build build/qt --target virtualbasys_qt_qmllint virtualbasys_qt_stopwatch_qmllint
./build/qt/src/qt/virtualbasys_qt --smoke-test
./build/qt/src/qt/virtualbasys_qt_stopwatch --smoke-test
```

Controller tests use a recording engine and injected monotonic clock for exact
batch/wait/measurement oracles, reset and UART preservation, overflow and thread/
reentrancy checks. Real counter schedules compare complete logs across direct,
stepped and paced execution. The stopwatch runs the unchanged 300-million-cycle
golden with non-grid batches and pacing changes, then verifies physical reset and
display persistence. UI tests send actual mouse/key events and exercise the real
Qt timer, Pause, Step, Reset, pacing and minimum-window geometry.

Capture the controls with:

```sh
VB_QT_SCREENSHOT_DIR="$PWD/build/qt/control-captures" ./build/qt/src/qt/test_qt_simulation_ui
```

Native interaction tests need foreground isolation; see [qt_board.md](qt_board.md).
The running screenshot uses the real clock and timer, not injected measurements.

## Performance reproduction

```sh
cmake -S . -B build/qt -DVB_BUILD_QT_GUI=ON -DVB_BUILD_GUI=OFF -DVB_BUILD_BENCHMARKS=ON
cmake --build build/qt --target benchmark_qt_control_counter benchmark_qt_control_stopwatch -j 8
./build/qt/bench/benchmark_qt_control_counter 20000000 3
./build/qt/bench/benchmark_qt_control_stopwatch 20000000 3
```

Run the benchmarks serially on an otherwise idle machine, keeping each window
in the foreground. They use the application's Basic style and compare direct
BoardModel batches, the controller event loop, and the same QML board with native
rendering, then measure the rendered 1× target. Paths share a 16-cycle reset and
2-million-cycle warmup; path order rotates between runs. Event-loop paths may
slightly exceed the requested cycle count, so CSV records actual cycles and wall
time. Frame counts are Qt presentations, not simulated VGA frames. Performance
thresholds are deliberately absent from correctness tests.
Set `VB_QT_SCREENSHOT_DIR` to capture paused rendered results after timing ends.

Measured 2026-09-22 on macOS 26.5.2 arm64, Qt 6.11.2, default CMake build type
with the existing backend/generated-code `-O2`. Three samples per path, 20M measured
cycles each; table entries are medians. Native window activation was supervised
through macOS System Events during window handoffs. These are small local samples,
not hardware-independent speed or frame-rate promises.

| Design | Direct board, Mcycles/s | Controller, Mcycles/s | Rendered turbo, Mcycles/s | Rendered 1× target, Mcycles/s | Turbo Qt presentations/s |
| --- | ---: | ---: | ---: | ---: | ---: |
| Counter | 21.188 | 16.536 | 15.824 | 15.953 | 53.80 |
| Stopwatch | 17.761 | 14.431 | 13.236 | 14.360 | 41.17 |

[Raw samples](qt_control_performance.csv) include actual cycles, elapsed wall time,
multiplier and presentation counts. Rendered turbo is roughly 25% below its paired
direct-board baseline, including the deliberate GUI yield and rendering. The
counter's direct path remains near the historical P3/M1 ~21.8 Mcycles/s baseline.
Both examples remain well below 100 MHz; the 1× target cannot make them run faster.
Differences between the two rendered modes are not isolated from scheduling noise.

A separate foreground comparison selected the 1 ms yield: zero-delay rescheduling
produced roughly 7–13 presentations/s at ~20 Mcycles/s; the 1 ms yield produced
roughly 53–54 presentations/s at ~15–16 Mcycles/s. This is one sample per path,
recorded in [the yield comparison](qt_control_yield_comparison.csv), and explains
the responsiveness/throughput tradeoff without claiming a precise universal cost.

M4 verification: combined suite 30/30, frontend-free headless suite 18/18, Qt-only
Release ASan/UBSan suite 12/12 (`ASAN_OPTIONS=detect_leaks=0`), clean QML lint for
both launchers, and native embedded smoke checks. Controller cases pass 14/14 and
native UI cases 8/8, including QtTest lifecycle checks. Two independent reviewers
passed the final controller. Their reproduced nested-event scheduling issue is
covered by a regression. Backend, RTL, simulator invariants and existing goldens
remain unchanged.
