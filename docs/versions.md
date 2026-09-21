# Pinned toolchain versions

Update this record when bumping dependencies. The future GitHub Actions
workflow must pin these same versions.

| Tool | Version | Source | Notes |
|---|---|---|---|
| Verilator | 5.050 (2026-07-01) | Homebrew | build + test dependency |
| CMake | 4.0.0 | Homebrew | `find_package(verilator)` + `verilate()` verified against it |
| Apple clang | 21.0.0 (clang-2100.1.1.101) | Xcode CLT | C++20 |
| Yosys | 0.66 | Homebrew | installed, first used in phase 3 (synthesis / NetlistEngine) |
| SDL2 | sdl2-compat 2.32.70 | Homebrew | GUI; found via find_package(SDL2 CONFIG) |
| Dear ImGui | v1.92.8 | git submodule (third_party/imgui) | pinned to release tag; SDL2+Metal in-tree backends |
| Qt | 6.11.2 | Homebrew `qtbase` + `qtdeclarative` | optional Qt frontend; minimum 6.5; Core/Gui/Quick/Qml/QuickControls2, Test for adapter tests |
| Surfer | 0.7.0 | Homebrew | VCD acceptance gate (headless `surfer server`); NOTE: Homebrew's gtkwave cask is DISABLED upstream — GTKWave only via GitHub-release app, manual |
| macOS | Darwin 25.5.0, arm64 | — | Apple Silicon primary target |

## Verilation flags (all designs)

`--public-flat-rw --trace-vcd --timescale 1ns/1ns` (+ `-Wall` for shipped
examples only — never for user designs, whose diagnostics pass through
verbatim).

- `--public-flat-rw` is mandatory: VerilatorEngine resolves signals through
  the runtime symbol table, which is empty without it. Known cost: every
  module-scope signal gets a kept shadow variable + change-trigger sync, and
  public signals inhibit inlining/const-folding. Trivial for small designs;
  if the 1.6 VGA perf target (>= 10 simulated fps) is missed, the phase-2
  mitigation is selective `/*verilator public_flat_rw*/` metacomments.
- `--timescale 1ns/1ns`: Verilator's default precision is 1 ps, which would
  render our 10 ns cycles as 10 ps in every VCD.
- `--trace-vcd`: compiled-in trace support costs some eval speed even when
  not dumping; acceptable for phase 1.

## Performance baseline (measured 2026-07-14, M-series Mac, Verilator 5.050)

With the sim_* libraries at OPT_FAST/OPT_GLOBAL -O2: ~20 Mcycles/s raw
stepping through VerilatorEngine (two evals/cycle) on the stopwatch;
~15.7 Mcycles/s effective under golden-test conditions (1000-cycle grid
sampling + structured logging). That is **0.2x real-time** for the 100 MHz
board clock — the "counter/stopwatch examples should exceed real-time" performance guide
is NOT currently met; the phase-2 speed indicator ("sim speed: N MHz — 0.NNx
real-time") starts from these numbers. Without the -O2 verilate flags the
same run is ~3.3 Mcycles/s (~90 s golden test) — do not remove them.

Flag-cost decomposition (measured 2026-07-15, identical 100M-cycle harness,
stopwatch, -O2):

| verilation | Mcycles/s | vs real-time |
|---|---|---|
| bare (ports via members; no public, no trace) | 29.8 | 0.30x |
| + `--public-flat-rw` | 21.6 | 0.22x |
| + `--trace-vcd` compiled in, dumping OFF | 21.6 | 0.22x |

Conclusions: compiled-in trace support is FREE when not dumping (the earlier
"costs some eval speed" note above is wrong for 5.050 — kept for history);
`--public-flat-rw` costs ~28%; and the dominant limit is Verilator's per-eval
overhead on tiny designs at two evals/cycle — even the bare ceiling is 0.3x
real-time. The speed indicator should not promise what the ceiling cannot
deliver.

Note on the single-eval "~2x" lever (milestone 1.6): it was VERIFIED
NON-VIABLE. A correct single-eval-per-cycle needs Verilator's clock to have
been recorded low by a prior eval, so eliding the negedge eval breaks edge
detection — measured directly: a posedge counter reads q=1 instead of q=1000
under naive single-eval, and the trigger-prev variable is not in the public
scope (poking it would be version-fragile internals-reaching). So milestone
1.6 shipped the honest-banner posture (A), not a single-eval engine mode.
Selective `public_flat_rw` metacomments (~1.4x) remain the one clean lever,
but they help only OUR annotated demo RTL, not a user's standard VGA design —
so they were NOT taken (a margin true for the screenshot and false for users
is not a real margin).

## VGA frame-rate baseline (milestone 1.6, measured 2026-07-16, M-series Mac)

Measured through the exact BoardModel::tick VGA path (3 warmed runs):

| path | Mcyc/s | VGA fps |
|---|---|---|
| raw step() ceiling (no pixel capture) | ~18 | ~10.8 |
| engine.stepCapture (14-bit VGA watch set) | ~15.8 | ~9.4 |
| full tick (grid+UART split + stepCapture + assembler) | ~15.0 | ~9.0 |
| integrated GUI (with SDL/Metal present) | — | ~8.6 |

The frame-producing path is stepCapture-bound at **~9 fps** — JUST UNDER the
">= 10 simulated fps" VGA performance guide on this hardware. That is a guide, not
a promise: the demo renders frame-accurately and the honest speed banner
shows the real per-frame fps and multiplier (never smoothed), so a run under
load reads e.g. "8.6 fps — 0.15x real-time" truthfully. The milestone's
Definition of Done — the pixel-exact headless golden — is framerate-
independent and passes in ~0.8 s. The tap (vb_engine) and assembler
(vb_board) carry -O2 like the sim_* libraries (the default build type is
often empty/-O0); without it the demo runs ~2x slower.

## VCD viewer verification (R3 / milestone 1.4)

- Surfer 0.7.0 (Homebrew): automated gate in ctest (`stopwatch_vcd_surfer`)
  waits for headless-server "Loaded body" and fails on parse errors or loader
  panics.
- GTKWave: VERIFIED 2026-07-15 against GTKWave Analyzer v3.3.116 built from
  the GitHub release source (gtk+3 + tcl-tk@8 via Homebrew; one-line quartz
  patch: WAVE_USE_XID excluded on __APPLE__ — GtkPlug/GtkSocket are
  X11-only). stopwatch_trace.vcd (500k cycles) loaded cleanly; Tcl-scripted
  attestation printed maxtime=4999995 timedim=n (10 ns cycles — 100 MHz, not
  the 1.1-era 1 ps regression), clk samples at 5/10/15 ns = 1/0/1 (10 ns
  period confirmed), and all 31 facs of the TOP -> stopwatch hierarchy
  including internal registers (db_cnt, running, sel, BCD chain). This closes
  the GTKWave half of the 1.4 "opens in GTKWave/Surfer" criterion.
  (Homebrew's gtkwave cask remains disabled upstream; the build recipe above
  is the reproducible path. Caveat: the Tcl attestation needs an interactive
  Aqua session — Tk 8.6 throws NSInvalidArgumentException in headless/agent
  contexts, though GTKWave's loader still prints the matching "[4999995] end
  time" before Tk init.)
