# Pinned toolchain versions

Recorded when bumping (CLAUDE.md, CI section). The future GitHub Actions
workflow must pin these same versions.

| Tool | Version | Source | Notes |
|---|---|---|---|
| Verilator | 5.050 (2026-07-01) | Homebrew | build + test dependency |
| CMake | 4.0.0 | Homebrew | `find_package(verilator)` + `verilate()` verified against it |
| Apple clang | 21.0.0 (clang-2100.1.1.101) | Xcode CLT | C++20 |
| Yosys | 0.66 | Homebrew | installed, first used in phase 3 (synthesis / NetlistEngine) |
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
