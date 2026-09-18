# Current state

- Current milestone: P3 complete; pre-migration gate passed. Stop before Qt work.
- Completed: P0 planning/baseline, P1 correctness audit, P2 independent challenge,
  P3 regressions/fixes/fresh verification/invariants/performance. Historical 1.1–1.6 intact.
- Current architecture: per-design composition root constructs VerilatorEngine and
  PinBinding; BoardModel composes peripherals/logging; ImGui/SDL2/Metal accesses
  BoardModel. Pure C++ GUI scheduler preserves exact startup/script cycles.
- Target architecture: Verilator/future NetlistEngine → SimEngine → BoardModel →
  thin Qt adapter → QML. Qt has not been added; legacy frontend remains.
- Fixes: XDC literal parsing/serialization/clocks/ranges/top-level binding; UART false
  starts/continuous echo; invalid buttons; VGA vertical polarity/coincident sync;
  exact startup inputs; bounded, environment-independent waveform-viewer gate.
- Tests: final GUI-enabled build and 18/18 CTests pass; clean headless Release with
  AddressSanitizer/UndefinedBehaviorSanitizer also 18/18 (halt_on_error=1).
  Two fresh fix verifiers PASS; independent raw-wire UART, VGA phase and Tcl checks pass.
  All four legacy GUI finite runs pass; original UART/stopwatch goldens unchanged.
- Performance: counter 21.838, stopwatch 16.520, UART 18.590, VGA 14.910 Mcycles/s;
  VGA 8.875 frames/s. Reproducible method/raw samples in pre_migration_performance.md/.csv.
- Known blockers: none for M0. Limits remain documented: two-state/single master
  clock, literal-only XDC, fixed board UART/640x480 VGA, grid sampling, unbounded
  UART/log collections, no concurrent engine/board access. Real-time guide unmet.
- Important invariants: 10 ns/master cycle; low/high evaluation; step(0) no-op;
  poke/peek no time advance; tick(0) applies due RX; absolute 1000-cycle observations;
  exact RX edges; inclusive 2M-cycle display persistence; chunk invariance;
  v2 log order/goldens; RGB888 top-origin VGA and frame-1 stamp 4,928,013.
- Next concrete tasks (future session): M0 only—decide Qt dependency setup, add optional
  VB_BUILD_QT_GUI infrastructure alongside legacy, build both, full tests, fresh review.
  Before M1/M4 decide QObject/thread ownership and BoardModel reset/inspection APIs.
- Latest commit: HEAD, "docs: finalize pre-migration audit and simulator invariants".
  Resolve its hash with git rev-parse HEAD; focused fix hashes are in implementation_history.md.
