# VirtualBasys GUI migration roadmap

## Goal and architecture
Verify and freeze the simulator before incrementally replacing the frontend.
Target: Verilator / future NetlistEngine → SimEngine → BoardModel → thin Qt
adapter → QML. QML must never obtain engine objects or peripheral timing logic.
Preserve C++20 abstractions, cycle semantics, byte/pixel outputs and existing goldens.

## Pre-migration milestones (completed)
- P0: inspect repository; build legacy GUI and record the baseline test results.
- P1: code-and-test audit of engine, board/peripherals, XDC, integration and frontend
  boundaries; classify coverage and add independent adversarial regression cases.
- P2: independently review audit conclusions and run additional checks.
- P3: reproduce and fix confirmed bugs in small backend-only changes, independently
  verify, freeze invariants and measure counter/stopwatch/UART/VGA performance.

Migration gate: full existing and audit suites pass; confirmed correctness bugs are
resolved; independent review has no blockers; invariants and reproducible performance
baseline exist. No Qt implementation in P0–P3. Unmet requirements keep the gate closed.

## Qt milestones
| Milestone | Scope | Acceptance |
|---|---|---|
| M0 | Qt 6 infrastructure alongside legacy GUI; VB_BUILD_QT_GUI | Both optional frontend builds and existing suite pass |
| M1 | Thin QObject adapter and focused Qt models over BoardModel | Headless adapter tests; no engine exposure to QML |
| M2 | IDE shell, navigation, board, inspector, bottom panels | Functional controls only; empty/error states |
| M3 | Reusable switches, LEDs, buttons, seven-segment QML | Counter/stopwatch match frozen behavior |
| M4 | Run/pause/step/reset/time/throughput/pacing | Chunk invariance and honest speed; two independent reviewers |
| M5 | UART terminal, send/clear/scrollback/time | Existing byte-exact regressions unchanged |
| M6 | Efficient VGA texture/QQuickItem integration | Pixel-exact output and measured performance; two reviewers |
| M7 | Batched inspector/log models, filtering, autoscroll | Stable values/order; no excessive per-signal QML calls |
| M8 | Layout/theme/status polish, screenshots, parity then legacy removal | Independent parity comparison, two removal reviewers, clean build/full suite |

Each milestone requires a successful build, relevant and full tests, invariant and
performance comparisons, independent review, and resolution of blocking findings.
Existing goldens remain the acceptance baseline.

## Dependencies and unresolved decisions
- Existing Verilator 5.050, CMake, C++20; legacy SDL2/Metal remains until parity.
- Optional Qt ≥6.5 Core/Gui/Quick/Qml/QuickControls2; VB_BUILD_QT_GUI defaults OFF.
- M1 uses a C++-owned adapter borrowing BoardModel on the GUI thread; QML sees
  cached child models. M4 keeps fixed-batch scheduling on that thread; each
  built-in example has its own launcher and immutable board/adapter lifetime.
- M4 reset uses the existing BTNC input and tick API for a 16-cycle pulse without
  rewinding time. Future trace/inspection APIs must also stay behind BoardModel.
- M5 groups the board's own UART stamps into a bounded adapter scrollback
  (1,000 rows, 4,096 pending input bytes). The UART launcher applies the Reset
  control once at startup because uart_echo needs its btnC reset (R6).
- M7 inspection stays behind BoardModel (read-only pass-throughs). The inspector
  lists ports and user watches; internal signals are not enumerated, because
  SimEngine enumerates only ports. The Qt log view owns the structured log while
  recording, so the board's copy stays bounded. The output panel is a
  StackLayout so pages never appear before layout.
- M6 chose the scene-graph texture item over a painted QImage by measurement: a
  new frame costs 0.40 ms against 1.56 ms of render-thread time, with equal
  throughput. The display snaps to device pixels, which Metal requires for exact
  pixels at fractional layout positions (docs/qt_vga.md).
- XDC remains a bounded literal subset; declared HDL indices are preserved separately
  from packed signal values. See the frozen simulator invariants.
