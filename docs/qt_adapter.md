# Qt BoardModel adapter

M1 introduces the `VirtualBasys.Board` QML module. Its `BoardAdapter`,
`BoardIoModel` and `SevenSegmentModel` types are uncreatable from QML: C++ supplies
the adapter through the root window's required, typed `board` property. M3 supplies
the [board widgets](qt_board.md); M4 connects the built-in counter/stopwatch through
a separate [SimulationController](qt_control.md). `--preview` supplies a disconnected adapter.

## Ownership and threading

The composition root owns the engine, then BoardModel, then BoardAdapter, then
SimulationController and the QML engine. Destroy them in reverse order. BoardAdapter borrows a nullable
BoardModel pointer fixed for its lifetime; it never deletes the board. Models
are QObject children of the adapter. The launcher explicitly selects C++ ownership
for its adapter before injecting it with `setInitialProperties()`.

All objects and board access stay on the construction/GUI thread. Moving the
adapter or accessing BoardModel concurrently is unsupported. Adapter input calls
and refresh reject the wrong thread, even in Release builds. A future worker
controller would require a separate ownership/command/snapshot design; M4 stays on the GUI thread.
Replacing a design requires replacing its adapter with proper teardown; M1 has
no attach/detach or asynchronous lifetime mechanism.

## Presentation API

`connected`, `hasDisplay` and the four model pointers are constant, read-only
properties. Binding availability cannot change during a BoardModel's lifetime.
Changing values use the models' `dataChanged` notifications.

| Property | Rows | Roles |
|---|---|---|
| switches | 16, SW0–SW15 | resource, available, active |
| leds | 16, LED0–LED15 | resource, available, active |
| buttons | 5, BTNC/BTNU/BTNL/BTNR/BTND | resource, available, active |
| digits | 4, AN0–AN3 order | digit, segments, decimalPoint, character |

All model roles are read-only. An unavailable I/O resource retains its slot and
reads inactive. Digit values come from BoardModel's fused display; `hasDisplay`
means at least one anode is bound, as in the existing board API. Unbound digits
remain dark. Segment masks use the existing lit-bit convention, not raw active-low
pins. `Qt::DisplayRole` supplies the resource name or decoded digit character.

`setSwitch(int, bool)` and `setButton(int, bool)` delegate to BoardModel and
immediately refresh the presentation snapshot. They return true for accepted
writes, including idempotent writes; false for disconnected/unbound/out-of-range
resources, wrong-thread calls or reentry during notification. Buttons persist
until released through the same method. M5 adds `sendUartText(QString)` and
`clearUart()` for the read-only `uart` terminal model, with the same thread and
reentry guards; see [qt_uart.md](qt_uart.md). M6 adds the read-only `vga` frame
model, which copies the framebuffer only when a new frame completes, and the
`VgaDisplay` item; see [qt_vga.md](qt_vga.md). M7 adds the `inspector` and
`eventLog` models with their `inspectorView` and `eventLogView` filter proxies,
and the `addWatch`, `removeWatch`, `setLogRecording` and `clearEventLog`
methods; see [qt_inspector_log.md](qt_inspector_log.md).

The C++-only `refresh()` reads a complete snapshot, stages all model caches,
then publishes changed rows and roles. New UART terminal rows are inserted last,
because list insertions must be announced as they happen. Adjacent equivalent changes are grouped;
unchanged refreshes emit nothing. Signal handlers see all new cached values.
Reentrant writes/refreshes are rejected until notification completes.

Model reads never access BoardModel. Refresh does not call `tick`, including
`tick(0)`, and never advances cycles or consumes queued UART edges. LED reads can
settle pending combinational/asynchronous logic through BoardModel's normal peek
semantics. Display fusion, decoding and virtual-time persistence remain in the
backend. Scheduling and physical reset are separate controller commands. No BoardModel,
binding, signal handle or engine object is exposed to QML.

## Verification and performance

`qt_board_adapter` uses a headless QCoreApplication, Qt model contract checking,
signal spies, an analytical pin source and an actual QQmlComponent. It covers
remapped resources, cached values, precise notifications, rejected writes, thread
and reentry guards, fused display decay and the typed QML boundary. `qt_counter`
checks real Verilator arithmetic and compares logs across different tick/refresh
cadences. Existing simulator tests and goldens remain the backend oracle.

Known test diagnostic on Qt 6.11.2: QtTest's model tester can emit an invalid GUI
metatype warning from `testDataGuiRoles` in the QCoreApplication-only test.
Independent review traced it to QtTest's GUI-role probe; model assertions and
ownership checks pass. The adapter tests intentionally need no GUI platform.

The optional paired benchmark measures direct BoardModel stepping against the
same 100,000-cycle chunks with one adapter refresh per chunk:

```sh
cmake -S . -B build/qt -DVB_BUILD_QT_GUI=ON -DVB_BUILD_BENCHMARKS=ON
cmake --build build/qt --target benchmark_qt_adapter -j 8
./build/qt/bench/benchmark_qt_adapter 20000000 3
```

Both paths use fresh counter engines, a 16-cycle reset and a 2M-cycle warmup.
Run order alternates; cycle accounting and arithmetic are checked in both paths.
This measures adapter publication without rendering or a QML view. It is not a
GUI frame-rate measurement or a speed threshold in the test suite.

Measured 2026-09-18 on the recorded macOS arm64/Qt 6.11.2 toolchain, default build
type (existing backend/generated-model `-O2`, default adapter flags): median
**21.754 Mcycles/s** direct versus **21.599 Mcycles/s** with refresh, approximately
**0.7% lower** in this three-pair sample. The small difference is not isolated
from run-to-run noise. [Raw samples](qt_adapter_performance.csv) use the command
above; the direct path is close to the P3 counter baseline of 21.838 Mcycles/s.

Qt references: [C++ type registration](https://doc.qt.io/qt-6/qtqml-cppintegration-definetypes.html),
[list model contracts](https://doc.qt.io/qt-6/qabstractlistmodel.html),
[model contract tester](https://doc.qt.io/qt-6/qabstractitemmodeltester.html).
