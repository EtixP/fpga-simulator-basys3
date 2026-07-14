# Pinned toolchain versions

Recorded when bumping (CLAUDE.md, CI section). The future GitHub Actions
workflow must pin these same versions.

| Tool | Version | Source | Notes |
|---|---|---|---|
| Verilator | 5.050 (2026-07-01) | Homebrew | build + test dependency |
| CMake | 4.0.0 | Homebrew | `find_package(verilator)` + `verilate()` verified against it |
| Apple clang | 21.0.0 (clang-2100.1.1.101) | Xcode CLT | C++20 |
| Yosys | 0.66 | Homebrew | installed, first used in phase 3 (synthesis / NetlistEngine) |
| SDL2 | sdl2-compat 2.32.70 | Homebrew | GUI; found via find_package(SDL2 CONFIG) |
| Dear ImGui | v1.92.8 | git submodule (third_party/imgui) | pinned to release tag; SDL2+Metal in-tree backends |
| Surfer | 0.7.0 | Homebrew | VCD acceptance gate (headless `surfer server`); NOTE: Homebrew's gtkwave cask is DISABLED upstream — GTKWave only via GitHub-release app, manual |
| macOS | Darwin 25.5.0, arm64 | — | Apple Silicon primary target |

## Verilation flags (all designs)

`--public-flat-rw --trace-vcd --timescale 1ns/1ns` (+ `-Wall` for shipped
examples only — never for user designs, whose diagnostics pass through
verbatim per R5).

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
board clock — R1's "counter/stopwatch examples should exceed real-time" guide
is NOT currently met; the phase-2 speed indicator ("sim speed: N MHz — 0.NNx
real-time") starts from these numbers. Without the -O2 verilate flags the
same run is ~3.3 Mcycles/s (~90 s golden test) — do not remove them.

## VCD viewer verification (R3 / milestone 1.4)

- Surfer 0.7.0 (Homebrew): automated gate in ctest (`stopwatch_vcd_surfer`)
  waits for headless-server "Loaded body" and fails on parse errors or loader
  panics.
- GTKWave: Homebrew cask disabled upstream; manual verification via the
  GitHub-release app is PENDING — recorded here so the half-verified state of
  the "opens in GTKWave/Surfer" criterion is explicit, not implied.
