# Current state

- Current milestone: M2 complete, independently verified and committed. Stop before M3.
- Completed: P0–P3 audit/fixes/invariants/performance, M0 optional Qt infrastructure,
  M1 thin Qt adapter and fixed presentation models. Historical 1.1–1.6 intact.
- Current architecture: SimEngine → BoardModel → BoardAdapter → cached Qt models →
  QML. Legacy ImGui/SDL2/Metal still uses BoardModel directly. The Qt launcher injects
  a disconnected typed adapter; design loading and interactive board controls are deferred.
- M2 shell: toolbar, project navigation, board/overview workspace, inspector and
  Terminal/UART/Logs/Waveforms tabs. Resizable/hidden panes, keyboard shortcuts,
  focus recovery and layout reset work; empty/missing-adapter states are explicit.
  Layout is session-only; Basic controls use a shared dark palette. No simulation
  controls, polling, refresh or input writes were added. See qt_build.md.
- Ownership: C++ owns simulation engine, BoardModel, adapter, then QML engine;
  reverse destruction order. Adapter borrows a fixed nullable BoardModel; child
  models belong to adapter. All remain on the construction/GUI thread; no concurrency.
- Qt boundary: only switch/button inputs are invokable; model roles are read-only.
  C++ refresh stages every cache before notifying changed rows/roles; reads/refresh
  never advance cycles or call tick(0). LED reads may settle pending inputs through
  BoardModel. Reentrant writes/refreshes and wrong-thread mutations are rejected.
- Build: VB_BUILD_QT_GUI defaults OFF, independent of legacy VB_BUILD_GUI (ON).
  Qt ≥6.5, tested 6.11.2; Core/Gui/Quick/Qml/QuickControls2 and Test for Qt tests.
  Qt discovery/autogen remain local. See qt_build.md and qt_adapter.md.
- M2 tests: combined frontends 22/22; Qt/SDL2-disabled headless 18/18;
  QML lint, native Qt first-frame smoke and native/offscreen shell interaction checks
  pass. Real clicks/keys cover navigation, tabs, focus recovery, splitters, reset,
  minimum-size/clipping geometry and null-adapter error state; captures inspected.
  Earlier M1 adapter ownership/thread/model checks and sanitizer results remain valid.
- Fresh M2 verifier: PASS, no issues. Independent 22/22, QML lint and 10/10 shell
  checks at 2× scale; 32 extreme layouts / 256 page-tab states and maximum-pane
  expansion followed by minimum-window shrink pass without clipping or overflow.
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
  Qt has no loaded design, board visuals, simulation scheduling/reset, live UART/VGA
  output or inspector/log data models yet. Standalone packaging
  deferred. Existing backend limits/real-time shortfall remain documented.
- Next concrete tasks (M3): reusable switches, LEDs, buttons and seven-segment QML,
  verified with counter and stopwatch. Await the next milestone request.
  Worker scheduling and board reset/inspection APIs remain later decisions.
- M1 implementation commit: 8d2af00, "feat: add tested Qt adapter and board presentation models".
- M2 implementation commit: 853198d, "feat: add Qt application shell with tested navigation and layout".
- Commit attribution: human Git identity only; historical AI coauthor trailers
  removed, with future commit/session attribution disabled in .claude/settings.json.
- Latest commit: HEAD, "docs: record verified M2 shell milestone".
  Resolve with git rev-parse HEAD; milestone hashes are in implementation_history.md.
