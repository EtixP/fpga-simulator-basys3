# Current state

- Current milestone: M0 complete, independently verified and committed. Stop before M1.
- Completed: P0 planning/baseline, P1 correctness audit, P2 independent challenge,
  P3 fixes/verification/invariants/performance; M0 optional Qt infrastructure.
  Historical 1.1–1.6 intact.
- Current architecture: per-design VerilatorEngine/PinBinding → BoardModel → legacy
  ImGui/SDL2/Metal. Optional Qt Quick launcher embeds a QML window; it links only Qt
  and has no simulation objects. Legacy frontend remains available.
- Target architecture: Verilator/future NetlistEngine → SimEngine → BoardModel →
  thin Qt adapter → QML. Engine objects and peripheral timing never enter QML.
- Decisions: VB_BUILD_QT_GUI defaults OFF, independent of VB_BUILD_GUI (default ON).
  Qt ≥6.5 Core/Gui/Quick/Qml/QuickControls2; tested Qt 6.11.2 on macOS arm64.
  Qt discovery/autogen stay local to src/qt. Development builds use installed Qt;
  standalone packaging is deferred. Build/run instructions: qt_build.md.
- M0 tests: combined frontend build 19/19; legacy-only 18/18 with Qt lookup disabled;
  clean headless 18/18 with Qt/SDL2 lookup disabled; Qt-only build/render smoke passes.
  QML lint, native macOS Qt first-frame smoke, all four legacy finite screenshot runs
  pass. All 10 backend compiler commands match between Qt-enabled/headless builds.
- Fresh M0 verifier: PASS, no outstanding issues. Independent full suite 19/19;
  negative import/root/early-quit/no-render deadline checks fail as intended.
- Simulator: no backend, RTL, existing test, invariant or golden changes in M0.
  P3 clean headless Release ASan/UBSan 18/18 and two fresh PASS reviews remain valid.
- Performance: P3 counter 21.838, stopwatch 16.520, UART 18.590, VGA 14.910 Mcycles/s;
  VGA 8.875 frames/s. No M0 simulation benchmark: launcher has no simulation path,
  backend code/flags unchanged. Method/raw samples: pre_migration_performance.md/.csv.
- Known blockers: none. Qt window has only an empty
  state, no board adapter/controls yet. Existing limits: two-state/single master clock,
  literal XDC, fixed UART/VGA, grid sampling, unbounded UART/log collections,
  single-threaded engine/board access; real-time performance guide remains unmet.
- Important invariants: 10 ns/master cycle; low/high evaluation; step(0) no-op;
  poke/peek no time advance; tick(0) applies due RX; absolute 1000-cycle observations;
  exact RX edges; inclusive 2M-cycle display persistence; chunk invariance;
  v2 log order/goldens; RGB888 top-origin VGA and frame-1 stamp 4,928,013.
- Next concrete tasks (M1): decide QObject/thread ownership; add a thin BoardModel adapter with
  focused models and headless tests. Board-level reset/inspection APIs remain open.
- M0 implementation commit: 9f7224e, "feat: add optional Qt Quick frontend infrastructure".
- Latest commit: HEAD, "docs: record verified Qt infrastructure milestone".
  Resolve its hash with git rev-parse HEAD; milestone hashes are in implementation_history.md.
