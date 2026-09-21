# Current state

- Current milestone: M3 complete, independently verified and committed. Stop before M4.
- Completed: P0–P3 audit/fixes/invariants/performance, M0 optional Qt infrastructure,
  M1 thin Qt adapter/models and M2 application shell. Historical 1.1–1.6 intact.
- Current architecture: SimEngine → BoardModel → BoardAdapter → cached Qt models →
  QML. Legacy ImGui/SDL2/Metal still uses BoardModel directly. The Qt launcher injects
  a disconnected typed adapter and disabled complete board preview. Connected board
  widgets use only adapter inputs/cached models; design loading/control remain deferred.
- M2 shell: toolbar, project navigation, board/overview workspace, inspector and
  Terminal/UART/Logs/Waveforms tabs. Resizable/hidden panes, keyboard shortcuts,
  focus recovery and layout reset work; empty/missing-adapter states are explicit.
  Layout is session-only; Basic controls use a shared dark palette. See qt_build.md.
- M3 adds reusable switches, LEDs, momentary buttons and seven-segment digits, with
  physical resource ordering, usable scrollable control sizes and focus reveal.
  Visual state comes from the models; buttons track/release only UI-owned holds.
  No simulation scheduling, timers, peripheral decoding or persistence moved to QML.
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
- M3 tests: full combined suite 25/25; Qt/SDL2-free headless 18/18; Qt-only Release
  ASan/UBSan checks 7/7; clean QML lint and native embedded first-frame smoke.
  Analytical widgets 15/15 offscreen/native; real counter 5/5 native; native stopwatch
  3/3 including unchanged 300M-cycle golden and physical 01.41 display. Captures
  inspected. A reproduced stale button-ownership bug was fixed: an external release
  revokes the old UI hold. Existing shell checks still pass.
- Independent M3 verdict: PASS, no remaining findings. Fresh subreview passed
  25/25, lint and accessibility action probe; independent native runs, true adapter
  replacement, external-assertion preservation and synchronous-hide probes pass.
- Simulator: backend, RTL, existing tests, invariant document and goldens unchanged.
  Frozen semantics: 10 ns low/high cycles; step(0) no-op; zero-time poke/peek;
  absolute 1000-cycle observations; exact RX; inclusive 2M-cycle digit persistence;
  chunk invariance; v2 logs; RGB888 top-origin VGA with frame-1 stamp 4,928,013.
- Performance: paired counter median 21.754 Mcycles/s direct, 21.599 with one refresh
  per 100k cycles (~0.7% lower, small sample/noise not isolated). No rendering in this
  measurement. Reproduction/raw data: qt_adapter.md / qt_adapter_performance.csv.
  P3 representative baselines remain in pre_migration_performance.md/.csv.
  M3 adds no application execution loop/cadence, so rendered throughput awaits M4.
- Native interaction tests require foreground isolation: focus loss intentionally
  cancels held inputs. One concurrent native golden mismatch did not recur in
  isolated runs; controlled focus loss reproduces that symptom, without proving
  the original cause. Automated CTest uses offscreen isolation; see qt_board.md.
- Known blockers: none. QtTest 6.11.2 emits a benign GUI-metatype diagnostic in the
  QCore-only model tester; independent backtrace and details in qt_adapter.md.
  Qt has no loaded design, simulation scheduling/reset, live UART/VGA
  output or inspector/log data models yet. Standalone packaging
  deferred. Existing backend limits/real-time shortfall remain documented.
- Next concrete tasks (M4): integrate loaded examples and run/pause/step/reset,
  virtual time, measured throughput and honest pacing. Resolve controller ownership
  and BoardModel reset API without bypassing the board. Verify chunk invariance and
  use two independent reviewers. Await the next milestone request.
- M1 implementation commit: 8d2af00, "feat: add tested Qt adapter and board presentation models".
- M2 implementation commit: 853198d, "feat: add Qt application shell with tested navigation and layout".
- M3 implementation commit: 7437a04, "feat: add interactive Qt Basys board components".
- Commit attribution: human Git identity only; historical AI coauthor trailers
  removed, with future commit/session attribution disabled in .claude/settings.json.
- README.md summarizes M3 progress, preview limitations and captured Qt screenshots.
- Latest commit: HEAD, "docs: add concise project README and Qt screenshots".
  Resolve with git rev-parse HEAD; milestone hashes are in implementation_history.md.
