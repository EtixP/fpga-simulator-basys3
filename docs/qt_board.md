# Qt virtual board

M3 adds the complete Basys 3 presentation to the Qt workspace: 16 switches,
16 LEDs, five momentary buttons and four seven-segment digits. The normal launcher
shows a disabled preview because it does not load a design yet. Simulation control
arrives in M4; the existing ImGui demos remain the runnable interactive examples.
The Qt integration tests supply real counter and stopwatch designs from C++.

## Interaction and presentation

Only XDC-bound inputs are enabled. Click or press Space on a focused switch to
request its opposite state. Hold the mouse button or Space on a push button to
assert it; release to clear it. Visual state always comes from the adapter's cached
models, including external input changes and rejected writes. Unbound resources
retain their physical slots and stay dark/disabled.

Switches and LEDs run from 15 on the left to 0 on the right. Digits run from AN3
on the left to AN0 on the right; the stopwatch's AN2 decimal point yields SS.CC.
The button cross uses the existing C/U/L/R/D resource indices. Segment geometry
uses the adapter's lit-bit masks directly: A/top, B/upper right, C/lower right,
D/bottom, E/lower left, F/upper left, G/middle. The decimal point is independent.
Unrecognized segment patterns remain visible. QML does no character decoding,
multiplex sampling, brightness decay or simulation stepping.

The default layout fits the whole board. Narrower panes scroll while preserving
usable control sizes; keyboard focus reveals the focused input automatically.
The shell follows the OS keyboard-navigation preference. Test fixtures explicitly
enable full Tab navigation so every bound input is checked on macOS as well.

## Ownership and lifetime

The widgets consume `BoardAdapter` and its existing models; no engine object,
pin binding or mutable model role is available to QML. The C++ composition root
retains the [M1 ownership and threading contract](qt_adapter.md): QML dies before
the adapter, which dies before BoardModel and its engine.

`BoardSwitch` emits a request without keeping a second switch value. `BoardButton`
accepts a writer callback and retains it only when its own press succeeds; an
already asserted external input is not its release responsibility. Its accepted
hold ends on release, cancellation, focus loss, hiding/disabling, window
deactivation/minimization, callback replacement or destruction. Clearing the
input externally also ends the old hold's ownership. OS key repeats cannot
reassert a canceled hold. Replacement targets must respect the same C++ lifetime
contract until the old callback has been released.

There is no QML polling or simulation timer. C++ tests advance exact cycle counts
through BoardModel and explicitly refresh the adapter; clicks, scrolling,
navigation and painting advance no cycles. M3 introduces no application execution
loop or refresh cadence, so it makes no new throughput claim. P3/M1 benchmarks
remain the baseline; rendered simulation throughput belongs to M4's controller.

## Verification

M3 passed 25/25 checks with both frontends built, 18/18 with both frontends
disabled, and 7/7 Qt checks under Release ASan/UBSan. QML lint, native rendering
and independent review also passed. Simulator code and existing goldens were unchanged.

With the optional Qt build enabled, run:

```sh
ctest --test-dir build/qt -R 'qt_board_widgets|qt_board_counter_ui|qt_board_stopwatch_ui' --output-on-failure
```

The analytical widget test checks remapped resource inputs, output bits, independent
segment positions, virtual-time persistence, button ownership/cancellation and
keyboard scrolling. The counter test checks arithmetic and synchronous reset
through actual controls. The stopwatch test replays the unchanged three-second
golden through QML button events and checks the physical 01.41 display. Each real
RTL test links exactly one Verilated design. Existing backend tests and goldens
remain unchanged.

To capture the connected example boards using the native Qt platform:

```sh
VB_QT_SCREENSHOT_DIR="$PWD/build/qt/board-captures" ./build/qt/src/qt/test_qt_board_counter_ui
VB_QT_SCREENSHOT_DIR="$PWD/build/qt/board-captures" ./build/qt/src/qt/test_qt_board_stopwatch_ui
```

Add `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software` for display-free execution.
Run native interaction tests one at a time and keep their window in the foreground.
Focus loss intentionally cancels a held button, changing its input timestamp and
therefore the fixed-stimulus golden comparison. Use the isolated offscreen CTest
configuration for automated runs.
Source-QML interaction tests fail on QML warnings. The launcher smoke test separately
checks embedded QML and a rendered frame. Screenshots are review artifacts, not
platform-dependent pixel goldens.
