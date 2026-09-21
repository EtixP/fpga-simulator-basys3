# Implementation history

## P0–P2 — Plan, baseline and independent audit
- Inspected architecture and recorded migration roadmap before production changes.
- Original GUI build passed; suite 10/11 exposed Surfer logging-dependent timeout.
- Primary code/waveform audits found gaps hidden by passing goldens; fresh P2 verifier
  returned FAIL and added coincident VGA sync / sustained UART cases.
- Decision: complete P3 corrections first; no Qt work in this session.
- Roadmap commit: ff4ef99. Audit reference: pre_migration_audit.md.

## P3 — Engine contract and waveform gate
- Added independent initialization/memory/dual-edge/derived-clock/reset/width/context/
  trace oracles; viewer enforces log verbosity, deadline and child cleanup.
- Tests: engine contract/edges, structural VCD, real Surfer and harness cases pass.
- Verifier: fresh A/B PASS; existing clock and trace semantics unchanged.
- Commit: 16c507c.

## P3 — XDC literals and declared indices
- Fixed lost/misinterpreted literal constraints and serialization; validate clocks,
  preserve signed HDL ranges and restrict binding to supported top-level ports.
- Decision: bounded literal Tcl subset, never command evaluation; packed values unchanged.
- Tests: original/default 105 pins, syntax/roundtrip/negative cases, real RTL ascending/
  offset/negative/one-bit ranges; independent native-Tcl and sanitizer fuzz pass.
- Verifier: fresh A/B PASS after diagnostics/glob/grouped-option refinements.
- Commit: 4c199d0.

## P3 — UART correctness and board bounds
- Reject false start candidates; sustain continuous echo after full stop bits without
  cumulative transmitter delay; invalid Button values are safe.
- Tests: all 256 bytes/all grid phases, malformed recovery, RX edges, log/persistence/
  chunk boundaries; 2000-byte same-RTL stream; original UART golden unchanged.
- Verifier: fresh A/B PASS; independent raw-cycle TX oracle at fast/default divisors.
- Commit: 8a573c0.

## P3 — VGA synchronization
- Reject inverted Vsync and correct row reconstruction with coincident sync origins.
- Decision: preserve capture-slot frame timestamp convention and pixel format.
- Tests: gradients, lanes/divisors/phase, malformed recovery, BoardModel partitioning;
  original frame stamp 4,928,013 and golden image unchanged.
- Verifier: two fresh reviewers PASS, including independent sanitizer phase sweep.
- Commit: 855f4e1.

## P3 — Exact startup scheduling
- Moved shared startup/script stepping to engine-free C++; scripts/logs begin at zero.
- Decision: default reset [0,16), explicit BTNC scripts own reset, zero frames do no work.
- Tests: per-edge/endpoint/order/chunk/reset/UART headless tests; finite legacy GUI smoke.
- Verifier: two fresh reviewers PASS; scheduler accesses BoardModel only.
- Commit: ba13d4a.

## P3 — Performance baseline and migration gate
- Added optional headless benchmark and raw samples for all four examples.
- Median Mcycles/s: counter 21.838, stopwatch 16.520, UART 18.590, VGA 14.910 (8.875 fps).
- Final normal GUI-enabled suite 18/18; clean headless Release ASan/UBSan suite 18/18;
  no original golden modifications. Frozen invariants and recovery documents finalized.
- Verifier: A and B PASS, no blockers; M0 infrastructure is next, not started.
- Benchmark commit: 4ea3f10; context-finalization commit: 7fc93b1.

## M0 — Optional Qt infrastructure
- Added default-OFF VB_BUILD_QT_GUI alongside legacy: embedded QML window, Qt-only
  launcher, bounded first-render smoke test and reproducible build guide.
- Decision: Qt ≥6.5, tested 6.11.2; discovery/autogen confined to frontend directory.
  No simulation integration yet; adapters begin in M1. Standalone packaging deferred.
- Tests: combined frontends 19/19; legacy-only and clean headless each 18/18 with
  disabled dependency lookups; Qt-only smoke, QML lint, native Qt render and four
  legacy finite screenshot runs pass. Backend commands identical; invariants/goldens
  unchanged. Simulation performance remeasurement unnecessary: no simulation path added.
- Verifier: fresh PASS; independent 19/19 plus broken-import, wrong-root, early-exit
  and no-render deadline checks. Callback lifetime refinement reverified; no blockers.
- Commit: ea21f8a.

## M1 — BoardModel adapter and presentation models
- Added a C++-owned, GUI-thread adapter with four cached read-only board models,
  validated switch/button inputs and a typed QML boundary. Refresh advances no time.
- Decision: fixed borrowed BoardModel lifetime, child-owned models; stage all caches
  before precise notifications, reject reentrant/cross-thread writes. No controller yet.
- Tests: combined suite 21/21; Qt-free headless 18/18; Qt-only Release ASan/UBSan
  checks 3/3; QML lint/native Qt/four legacy finite runs pass. Independent pin oracles
  and real counter arithmetic/log comparisons preserve the unchanged backend/goldens.
- Performance: paired counter 21.754 Mcycles/s direct vs 21.599 with refresh/100k
  cycles (~0.7% lower; small sample, no rendering). Reproduction/raw samples recorded.
- Verifier: fresh PASS; independent full suite and ownership/GC/teardown/thread/
  notification probes. Nonblocking QtTest GUI-metatype diagnostic documented.
- Commit: 8d2af00.

## M2 — Qt application shell
- Added the toolbar, project navigation, board/overview workspace, inspector and
  Terminal/UART/Logs/Waveforms tabs with honest empty/missing-adapter states.
- Decision: presentation-only, session-local resizable layout; shared actions own
  visibility/shortcuts and recover keyboard focus. Restore layout resets sizes and
  selection. Board widgets, design loading and simulation controls remain deferred.
- Tests: combined suite 22/22; Qt/SDL2-disabled headless 18/18; clean QML lint and
  native Qt render. Eight shell cases pass offscreen/native (QtTest 10/10), covering
  actual clicks/keys, splitters, focus, tab bindings and clipped-content bounds.
  Native/default/minimum/hidden/error captures inspected. Backend/invariants/goldens
  unchanged; no simulation execution path changed, so throughput remeasurement
  is not applicable and the P3/M1 baselines remain the reference.
- Verifier: fresh PASS, no issues; independent 22/22, lint, 2×-scale shell 10/10,
  32 extreme geometries / 256 page-tab states and maximum-to-minimum resize pass.
- Commit: 853198d.

## M3 — Interactive Qt virtual board
- Added reusable switches, LEDs, momentary buttons and raw-mask seven-segment
  digits, physical ordering and keyboard-accessible scrolling. Launcher shows a
  disabled preview; connected counter/stopwatch fixtures verify real RTL inputs.
- Decision: cached models own visuals; no QML stepping/decoding/persistence.
  Release only accepted UI-owned holds on cancellation/focus/lifetime changes;
  regression fixed stale ownership after external release/reassertion. Application
  loading/control and rendered throughput remain M4; P3/M1 performance references
  apply without claiming a new rendering cadence or simulation speed.
- Tests: combined 25/25; Qt/SDL2-free headless 18/18; Qt-only Release ASan/UBSan
  7/7; clean lint/native embedded smoke. Analytical widgets 15/15 native/offscreen,
  real counter 5/5 and stopwatch 3/3 native; unchanged 300M-cycle golden and 01.41
  masks/placement verified. Backend, invariants and existing goldens untouched.
- Verifier: PASS; fresh full-suite/accessibility review and independent native,
  adapter-replacement, external-ownership and synchronous-hide probes pass. One
  concurrent native golden mismatch did not recur in isolated runs; forced focus
  loss reproduces the symptom (original cause unconfirmed). Foreground isolation
  is documented; CTest uses offscreen isolation. No remaining blocking findings.
- Commit: 7437a04.
