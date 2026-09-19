# Current state

- Current milestone: M1 complete, independently verified and committed. Stop before M2.
- Completed: P0–P3 audit/fixes/invariants/performance, M0 optional Qt infrastructure.
  Historical 1.1–1.6 intact; M1 adds the thin Qt adapter and fixed presentation models.
- Current architecture: SimEngine → BoardModel → BoardAdapter → cached Qt models →
  QML. Legacy ImGui/SDL2/Metal still uses BoardModel directly. The Qt launcher injects
  a disconnected typed adapter; design loading and interactive controls are deferred.
- Ownership: C++ owns simulation engine, BoardModel, adapter, then QML engine;
  reverse destruction order. Adapter borrows a fixed nullable BoardModel; child
  models belong to adapter. All remain on the construction/GUI thread; no concurrency.
- Qt boundary: only switch/button inputs are invokable; model roles are read-only.
  C++ refresh stages every cache before notifying changed rows/roles; reads/refresh
  never advance cycles or call tick(0). LED reads may settle pending inputs through
  BoardModel. Reentrant writes/refreshes and wrong-thread mutations are rejected.
- Build: VB_BUILD_QT_GUI defaults OFF, independent of legacy VB_BUILD_GUI (ON).
  Qt ≥6.5, tested 6.11.2; Core/Gui/Quick/Qml/QuickControls2 and Test for adapter tests.
  Qt discovery/autogen remain local. See qt_build.md and qt_adapter.md.
- M1 tests: combined frontends 21/21; Qt/SDL2-disabled headless 18/18;
  Qt-only Release ASan/UBSan 3/3 Qt checks. QML lint, native Qt first-frame smoke and
  all four legacy finite runs pass. All 10 backend compiler commands match with Qt
  enabled/disabled. Analytic models and real counter verify values/logs/cadence.
- Fresh M1 verifier: PASS; independent 21/21, QML lint, garbage collection/ownership,
  child teardown, moved-thread rejection and grouped-notification probes pass.
- Simulator: backend, RTL, existing tests, invariant document and goldens unchanged.
  Frozen semantics: 10 ns low/high cycles; step(0) no-op; zero-time poke/peek;
  absolute 1000-cycle observations; exact RX; inclusive 2M-cycle digit persistence;
  chunk invariance; v2 logs; RGB888 top-origin VGA with frame-1 stamp 4,928,013.
- Performance: paired counter median 21.754 Mcycles/s direct, 21.599 with one refresh
  per 100k cycles (~0.7% lower, small sample/noise not isolated). No rendering in this
  measurement. Reproduction/raw data: qt_adapter.md / qt_adapter_performance.csv.
  P3 representative baselines remain in pre_migration_performance.md/.csv.
- Known blockers: none. QtTest 6.11.2 emits a benign GUI-metatype diagnostic in the
  QCore-only model tester; independent backtrace and details in qt_adapter.md.
  Qt has no loaded design, board visuals,
  scheduling/reset, UART/VGA panels or inspector/log models yet. Standalone packaging
  deferred. Existing backend limits/real-time shortfall remain documented.
- Next concrete tasks (M2): introduce the IDE shell, navigation, board/inspector and
  bottom-panel areas with functional controls and honest empty states.
  Worker scheduling and board reset/inspection APIs remain later decisions.
- M1 implementation commit: 8d2af00, "feat: add tested Qt adapter and board presentation models".
- Commit attribution: human Git identity only; historical AI coauthor trailers
  removed, with future commit/session attribution disabled in .claude/settings.json.
- Latest commit: HEAD, "chore: use personal commit attribution".
  Resolve with git rev-parse HEAD; milestone hashes are in implementation_history.md.
