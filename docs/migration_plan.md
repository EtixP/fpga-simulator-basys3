# VirtualBasys GUI migration roadmap

## Goal and architecture
Verify and freeze the simulator before incrementally replacing the frontend.
Target: Verilator / future NetlistEngine → SimEngine → BoardModel → thin Qt
adapter → QML. QML must never obtain engine objects or peripheral timing logic.
Preserve C++20 abstractions, cycle semantics, byte/pixel outputs and existing goldens.

## Pre-migration milestones (this session only)
- P0: inspect repository; build legacy GUI and run full baseline suite; record plan/state.
- P1: code-and-test audit of engine, board/peripherals, XDC, integration and frontend
  boundaries; classify coverage and add independent adversarial regression cases.
- P2: fresh read-only verifier challenges audit conclusions and runs additional checks.
- P3: reproduce and fix confirmed bugs in small backend-only changes, independently
  verify, freeze invariants, measure counter/stopwatch/UART/VGA performance, commit.

Migration gate: full existing and audit suites pass; confirmed correctness bugs are
resolved; independent review has no blockers; invariants and reproducible performance
baseline exist. No Qt implementation in P0–P3. Unmet requirements keep the gate closed.

## Qt milestones (future sessions, one at a time)
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

Every milestone: build, relevant and full tests, invariants/performance comparison,
fresh skeptical verifier, resolve/reverify blockers, separate commit, update state/history.
Never weaken tests or regenerate golden data merely to hide failures.

## Dependencies and unresolved decisions
- Existing Verilator 5.050, CMake, C++20; legacy SDL2/Metal remains until parity.
- M0 will require Qt6 Core/Gui/Quick/Qml/QuickControls2; no new dependency now.
- Decide adapter/controller threading and QObject ownership before M1/M4.
- Define board-level reset/trace/inspection APIs without bypassing BoardModel.
- Choose VGA scene-graph/native texture versus QQuickItem/QImage after measurements.
- XDC remains a bounded literal subset; declared HDL indices are preserved separately
  from packed signal values. See the frozen simulator invariants.
