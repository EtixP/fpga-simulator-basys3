# CLAUDE.md — VirtualBasys: a Mac-native Basys 3 FPGA simulator

## Project overview

VirtualBasys is a macOS-native simulation tool for the Digilent Basys 3 (AMD Artix-7)
FPGA trainer board. Users write synthesizable Verilog targeting the real Basys 3 pinout,
and this tool simulates it against a virtual board GUI (switches, LEDs, seven-segment
display, buttons, UART, VGA) — no hardware, no Vivado, no VM.

The long-term differentiators:
1. A custom parallel gate-level simulation engine (phase 3) benchmarked against Verilator.
2. RTL-vs-gate-level co-simulation that detects simulation/synthesis mismatches.

## Architecture — the one rule that must never break

Everything hinges on a thin engine abstraction. The GUI and peripherals NEVER talk to
Verilator or the custom engine directly — only through `SimEngine`:

```cpp
class SimEngine {
public:
  virtual void step(uint64_t cycles) = 0;        // advance virtual time
  virtual uint64_t peek(SignalId s) = 0;         // read a signal
  virtual void poke(SignalId s, uint64_t v) = 0; // drive an input
  virtual void trace(bool enable) = 0;           // VCD/FST dump on/off
  virtual uint64_t now() const = 0;              // current cycle count
  virtual ~SimEngine() = default;
};
```

Two implementations, selected at runtime:
- `VerilatorEngine` (phase 1): wraps a Verilator-compiled design (RTL-level).
- `NetlistEngine` (phase 3): loads a Yosys-emitted gate-level JSON netlist and
  simulates it directly; parallelized with OpenMP/std::thread.

Data flow:
```
              user design (design.v + pins.xdc)
                             |
           +-----------------+------------------+
           |                                    |
    RTL path (phase 1)              gate-level path (phase 3)
           |                                    |
       Verilator                          Yosys synth -lut 6
  (compile RTL to C++)                   (emit netlist.json)
           |                                    |
    VerilatorEngine                       NetlistEngine
           |                                    |
           +-----------------+------------------+
                             |
                      SimEngine API
               (step / peek / poke / trace)
                             |
                  Board model (peripherals)
                             |
                   GUI  /  CLI  /  tests

  netlist.json additionally feeds two offline analyses (phase 3):
    - equivalence check: same stimulus through both engines, diff outputs (R4)
    - pseudo-STA: longest-path analysis of the netlist graph (R2)
```

If a proposed change would let peripheral or GUI code reach around `SimEngine`,
stop and refactor instead.

## Design requirements (non-negotiable)

### R1 — Timing model: cycle-accurate, honestly tracked virtual time
- Simulation is **cycle-accurate**, not gate-delay-accurate. One rising edge of the
  board's 100 MHz oscillator = 10 ns of virtual time. Do not attempt intra-cycle
  propagation delays.
- Maintain a single 64-bit virtual time counter owned by the engine. All peripherals
  derive timing from it (e.g., debounce = 10 ms = 1,000,000 cycles).
- Support derived clocks: users will generate a 25 MHz VGA pixel clock via clock
  dividers. The engine must evaluate correctly with multiple clock enables; document
  that we simulate a single master clock domain with enables, not true async CDC.
- **Wall-clock pacing is a GUI-layer concern only.** Two modes:
  - `--realtime`: throttle so virtual time ≈ wall time (interactive use).
  - `--turbo` (default for tests): free-run as fast as possible.
  Pacing must never alter simulated behavior. If a test passes in turbo and fails in
  realtime (or vice versa), that is a bug in the pacing layer.
- **Performance expectations (be honest with the user).** True real-time = 100M
  simulated cycles/sec, which is often NOT achievable for non-trivial designs even
  with Verilator on Apple Silicon. Therefore:
  - Realtime mode is BEST-EFFORT: throttle down when sim is too fast; when sim is
    too slow, run flat-out and display a live speed indicator
    ("sim speed: 31 MHz — 0.31x real-time"). Never silently pretend to be real-time.
  - VGA renders frame-accurately (every simulated frame is correct), not
    wall-clock-60fps. Frame pacing follows sim speed.
  - UART timing: in turbo mode, byte timing derives from virtual time (baud counted
    in cycles), so UART works identically regardless of wall-clock speed.
  - Rough perf targets (guides, not promises): counter/stopwatch examples should
    exceed real-time; VGA pattern should sustain >= 10 simulated fps on an M-series
    Mac in phase 1.

### R2 — Physical constraints via XDC files
- Parse the standard Basys 3 `.xdc` constraints format (same file users feed Vivado).
- **Pin constraints** (`set_property PACKAGE_PIN ...`): fully honored. The XDC binds
  top-level Verilog ports to virtual board resources. Ship the canonical Digilent
  Basys3_Master.xdc mapping as a built-in default (V17→sw[0], U16→led[0], W5→clk, etc.).
- **Timing constraints** (`create_clock -period ...`): phase 3 feature — pseudo-STA.
  This is STRUCTURAL analysis of the netlist graph; it involves no simulation and is
  entirely separate from the R4 functional output diff. Algorithm:
  1. Use the `synth -lut 6` technology-mapped netlist (LUT levels are only meaningful
     post-mapping).
  2. Build the timing graph from netlist.json: cells = nodes, nets = edges.
     Startpoints = FF Q pins and input ports; endpoints = FF D pins and output ports.
  3. Detect combinational loops first (error out — they are a design bug).
  4. Compute max LUT depth to every endpoint in one topological pass (DAG, linear time).
  5. Delay model: constant per-LUT-level estimate (~1 ns/level as an Artix-7
     order-of-magnitude heuristic; routing dominates logic) + optional fanout penalty.
  6. Slack = clock period (from create_clock) - worst path delay. Report the worst
     N paths with start/end registers and depth.
  Output is clearly labeled as an ESTIMATE: no placement/routing/clock-skew/setup-hold
  modeling. Relative depth is the useful signal, not absolute nanoseconds.
- Unknown/unsupported XDC commands: warn and skip, never crash.

### R3 — Debuggability is a first-class feature
- **Waveforms**: `--trace out.vcd` (and FST when using Verilator's FST writer).
  Verify output opens cleanly in GTKWave and Surfer on macOS. Preserve hierarchical
  signal names.
- **Logs**: pass through `$display`/`$write`/`$monitor`. Additionally emit a structured
  simulation log (cycle-stamped peripheral events: LED transitions, seven-seg digit
  changes, UART bytes, button presses). Format: `[cycle 142001] LED[3] 0->1`.
- **Interactive inspection** (phase 2): peek any signal from the GUI; conditional
  break ("pause when signal X == V").
- Every bug fixed in this project should, where feasible, add a regression test that
  would have caught it.

### R4 — Simulation/synthesis mismatch detection (co-simulation)
- This is FUNCTIONAL verification (does the netlist compute the same values as the
  RTL?), complementary to R2's pseudo-STA (can it compute them within the clock
  period?). Both consume the same netlist.json; neither replaces the other.
- We do NOT write a synthesis tool. Yosys is the synthesis tool.
- Instead: `virtualbasys check design.v` runs the SAME stimulus through both engines
  (RTL via Verilator, gate-level via Yosys+NetlistEngine) and diffs observable outputs
  cycle-by-cycle. Divergence = simulation/synthesis mismatch. Report first divergent
  cycle, signal, and both values.
- Known limitation to document honestly: Verilator is 2-state, so X-propagation
  mismatches are not fully detectable. Stretch goal: make NetlistEngine 4-state
  (0/1/X/Z) to catch X-related bugs (uninitialized regs, incomplete case).
- Also run `yosys -p "read_verilog design.v; proc; check"` style lint passes and
  surface inferred-latch warnings to the user prominently.

### R5 — Supported Verilog subset (the user contract)
- Target: the synthesizable Verilog-2005 subset that Vivado accepts for Basys 3
  coursework. Explicitly supported: modules/ports/parameters, always @(posedge clk),
  combinational always @(*), assign, if/case, arithmetic/logic/shift ops, arrays for
  inferred RAM/ROM, $readmemh/$readmemb for memory init, generate blocks.
- Explicitly deferred (error with a clear message, do not half-support):
  SystemVerilog-only constructs (interfaces, classes), tri-state internal buses,
  latches (warn loudly — usually a bug), true async multi-clock designs,
  Xilinx primitives instantiated directly (MMCM/PLL — provide a behavioral
  clock-divider idiom in docs instead).
- Testbench constructs ($display, delays, initial-only stimulus) are allowed in
  /tests, never required in user designs.
- Frontend errors: pass Verilator/Yosys diagnostics through verbatim, prefixed with
  which tool produced them. Never swallow or rewrite compiler errors.

### R6 — Initial state and reset semantics
- Power-on model: registers take their Verilog `initial`/declaration-assignment
  values, matching FPGA bitstream init behavior. Registers with no initial value are
  ZERO in both engines (2-state consequence) — document this divergence from real
  4-state simulators prominently.
- All shipped /examples use an explicit reset (tied to btnC by convention) and
  demonstrate good practice.
- The R4 equivalence checker must apply IDENTICAL initialization to both engines,
  or false mismatches will occur. If NetlistEngine later becomes 4-state, add an
  --x-init mode to deliberately surface uninitialized-register bugs.

## Phases and milestones

**Phase 1 — Verilator-backed virtual board (MVP)**
1. `SimEngine` interface + `VerilatorEngine` + headless test harness.
2. XDC pin parsing; port binding.
3. GUI (Dear ImGui + SDL2/Metal backend): 16 switches, 16 LEDs. Demo: counter design.
4. Seven-seg (4-digit multiplexed — must handle the ~1 kHz refresh mux correctly),
   5 buttons, VCD tracing, structured log.
5. UART console (TX first, then RX). Demo: echo design.
6. VGA: render 640x480@60 from simulated hsync/vsync/RGB into a texture.
   Demo: pattern generator or Pong.

**Phase 2 — Tool polish**
Realtime/turbo pacing, signal peek UI, conditional breakpoints, config/project files,
nice CLI (`virtualbasys run design.v --xdc pins.xdc --trace`).

**Phase 3 — Custom parallel engine + research writeup**
1. Yosys flow: `read_verilog; synth -lut 6; write_json` → load JSON netlist.
2. Single-threaded NetlistEngine. Correctness gate: bit-identical outputs vs
   VerilatorEngine on the full example suite before ANY parallelization.
3. Levelize combinational logic; parallelize per-level evaluation (OpenMP first,
   then compare std::thread pool). Explore partitioning strategies.
4. Benchmarks: speedup curves vs thread count on small/medium/large designs;
   compare against single-threaded self AND Verilator. Analyze sync overhead,
   cache behavior, Amdahl limits.
5. Mismatch checker (R4) built on top of the dual engines.

**Acceptance criteria (definition of done per milestone)**
- 1.1–1.2: headless test pokes sw[3:0], steps 1000 cycles, peeks led — golden values
  match; XDC parser round-trips Basys3_Master.xdc; `ctest` green.
- 1.3: counter demo runs in GUI; clicking a switch changes LEDs within one GUI frame.
- 1.4: stopwatch golden log matches for 3 simulated seconds, including correct
  digit-mux behavior at the ~1 kHz refresh rate; VCD opens in GTKWave/Surfer.
- 1.5: uart_echo round-trips "hello" at 9600 baud in turbo mode, byte-exact in log.
- 1.6: vga_pattern produces a pixel-exact PNG dump of frame 1 vs golden image.
- 3.2 gate: NetlistEngine output is bit-identical to VerilatorEngine on ALL /examples
  golden tests. No parallel work may begin before this passes.
- 3.4: benchmark script reproduces speedup CSV + plots from one command.

**CI**: GitHub Actions, macos-latest runner. Every PR: build + ctest + golden diffs.
Verilator/Yosys installed via Homebrew in the workflow; pin versions in the workflow
file and record them in docs/versions.md when bumping.

## Repo layout

```
/src
  /engine       SimEngine interface, VerilatorEngine, NetlistEngine
  /board        peripheral models (switches, leds, sevenseg, uart, vga, buttons)
  /constraints  XDC parser, pin binding
  /gui          ImGui/SDL2 frontend
  /cli          command-line entry points
/examples       counter.v, stopwatch.v, uart_echo.v, vga_pattern.v (+ .xdc each)
/tests          unit tests + golden-output regression tests
/bench          phase 3 benchmark designs and scripts
/docs           design notes, benchmark writeups
```

## Build & environment (macOS)

- Toolchain: clang++ (Apple), C++20, CMake ≥ 3.24.
- Dependencies via Homebrew: `brew install verilator yosys sdl2 cmake`.
  Dear ImGui vendored as a submodule.
- OpenMP on Apple clang requires `brew install libomp` and explicit flags — wire this
  into CMake early; do not assume `-fopenmp` just works.
- Build: `cmake -B build && cmake --build build -j`. Test: `ctest --test-dir build`.
- Target Apple Silicon first; keep code portable (no x86 intrinsics without fallback).

## Conventions for Claude Code

- Language: C++20 for the tool. Verilog examples must be synthesizable
  (Vivado-compatible subset); testbench-only constructs stay in /tests.
- Every peripheral model gets a headless unit test (no GUI needed) driving it through
  `SimEngine` with a scripted stimulus.
- Golden tests: each /examples design has an expected structured-log output; CI diffs.
- Never bypass `SimEngine` from board/GUI code. Never let GUI frame rate influence
  virtual time.
- Prefer small verifiable steps: when adding a peripheral, first make the headless
  test pass, then wire the GUI.
- When touching the NetlistEngine, run the cross-engine equivalence suite before
  and after; a parallelization change that alters outputs is wrong by definition.
- Ask before adding new dependencies.

## Decision log — verified facts, do not rediscover

Established in milestone 1.1–1.2 (2026-07-13) against Verilator 5.050; each is
enforced by code and/or a regression test. The canonical engine interface is
`src/engine/SimEngine.h`, which extends the snippet above with `lookup()`,
`info()`, `ports()`, and `setTraceFile()` (approved deviation).

- **`--public-flat-rw` is mandatory** for every verilated design: without it
  the runtime symbol table is empty and VerilatorEngine cannot resolve any
  signal. Cost notes in docs/versions.md.
- **Poke only through the "TOP.TOP" ports scope.** Module-scope vars
  ("TOP.<module>.x") are change-trigger-synced SHADOW COPIES of ports, not
  aliases: poking them appears to work until the real port changes. They are
  peek-only (and the only scope carrying real port directions is TOP.TOP).
- **Thread-context bind before eval/teardown.** `VerilatedScope::~VerilatedScope`
  unregisters from `Verilated::threadContextp()`, NOT from its own context —
  with two engines alive (R4 checker!), destroying them in non-LIFO order
  crashes unless the engine re-binds the thread context first.
  VerilatorEngine::bindThreadContext() handles this; keep it on every eval path.
- **`--timescale 1ns/1ns`** at verilation + timeInc(5) per half-cycle = honest
  10 ns cycles in VCDs (Verilator's default precision is 1 ps → 1000x off).
- **`ctx.traceEverOn(true)` at engine construction, unconditionally**: lazily
  enabling tracing later hard-aborts the process without it. `model.trace()`
  registers at most once per VCD writer; trace(false) = stop dumping + flush,
  never close/reopen (reopening truncates the earlier dump).
- **One eval() at construction** settles initial blocks/$readmemh/declaration
  inits — this implements the t=0 contract in SimEngine.h that both engines
  must match (R6).
- **Exactly ONE verilate()d library per test executable** — verilate()
  compiles the Verilator runtime into its target; linking two duplicates it.
  One `sim_<design>` static library per example design.
- **Tests use tests/check.h (CHECK/CHECK_EQ), never `<cassert>`** — assert()
  vanishes under NDEBUG and the suite goes vacuously green. Test data paths
  arrive as absolute argv from add_test, so binaries also run by hand.
- **Examples: explicit btnC reset, no declaration initializers** — Verilator's
  -Wall (PROCASSINIT) rejects mixing both; -Wall applies to shipped examples
  only, never to user designs (R5: verbatim diagnostics).
- **Binding is keyed on package pins, not port names** (tests/data/
  counter_swapped.xdc proves it); the shipped full-board default is
  mechanically derived from Digilent's master file (105 pins, 1 clock, 5
  [current_design] props — counts asserted in test_xdc).

Added in milestone 1.3 (GUI):

- **BoardModel is the GUI's only view of the simulation** (src/board/): it
  owns the PinBinding and exposes board state (setSwitch/ledState/tick/now).
  Draw code (src/gui/BoardWindow.cpp) has no direct SimEngine or Verilator
  include and no reachable engine object — a second, convention-enforced
  wall behind the SimEngine rule. New peripherals (seven-seg, UART, VGA)
  join BoardModel as siblings.
- **Frames advance virtual time by a FIXED cycle count** (GuiOptions::
  cyclesPerFrame, default 100'000; real-time ≈ 1.67M at 60 fps). Never
  "step until N ms elapsed" — tying cycle count to wall clock makes turbo
  and realtime diverge (R1). Honest pacing arrives in phase 2.
- **Dear ImGui is a submodule pinned to release tag v1.92.8** with the
  in-tree SDL2 + Metal backends; the .mm sources need -fobjc-arc; SDL2 comes
  from Homebrew's sdl2-compat via find_package(SDL2 CONFIG). When touching
  GUI toolchain wiring, verify a blank ImGui window renders before
  suspecting board logic.
- **Demo executables run headed-but-finite** (`--frames N --screenshot out.bmp`)
  and dump the final frame via Metal drawable readback
  (layer.framebufferOnly=NO) — no macOS screen-recording permission needed.
  The 1.6 VGA frame dump builds on this readback path.

Added in milestone 1.4 (seven-seg, buttons, structured log, stopwatch golden):

- **Output observation is an ABSOLUTE virtual-time grid**: BoardModel samples
  outputs exactly when now() crosses a multiple of kSampleChunkCycles (1000
  cycles = 10 us), never relative to tick() call boundaries — so the
  structured log is a pure function of (stimulus schedule, total cycles),
  invariant to frame sizes, driver refactors, and phase-2 pacing. The grid is
  the SLOW-peripheral observation mechanism (seven-seg ~kHz mux, LEDs, 1.5's
  UART at ~10.4k cycles/bit); 1.6's VGA (4 cycles/pixel) needs an engine-side
  tap, not a finer grid.
- **Seven-seg capture contract**: a digit is guaranteed captured iff its
  ACTIVE-anode window (dwell minus ghost-prevention blanking) spans >= 2
  chunks; sub-chunk active windows on chunk-multiple mux periods phase-lock
  to permanently dark (verified failure mode — documented, unsupported).
  Persistence window kPersistCycles = 2M (20 ms VIRTUAL time) exceeds
  Digilent's worst recommended 16 ms full-refresh period. Fused-display
  staleness bound: 4*dwell + one chunk.
- **Structured-log semantics (v=1, header frozen)**: outputs chunk-observed,
  inputs exact-cycle (a poke at N is first sampled at edge N+1),
  transition-only, canonical same-stamp order (SSEG digits 0..3 content line
  before .dp, then LEDs ascending), SSEG content is character-level.
  Collection is OPT-IN (a busy design otherwise accumulates ~40k lines/s).
  Chunk quantization is a fixed observation grid, NOT nondeterminism; the
  exact-cycle refinement path is engine change-callbacks (phase 2+).
  Changing kSampleChunkCycles is an expected-golden-churn event.
- **Buttons are momentary state with caller-owned duration**: setButton
  persists until cleared — GUI presses span frames via widget
  activate/deactivate edges; scripts hold via paired events. Debounce needs
  >= 1M cycles of stable input, so one-frame pulses are correctly swallowed
  (golden regression covers this).
- **Scripts are CYCLE-indexed** (`--at CYCLE:NAME=V`), never frame-indexed:
  frames are not a stable unit (cyclesPerFrame is tweakable; phase-2 pacing
  varies it). GuiApp splits frame ticks at event cycles; total cycles per
  frame stays fixed.
- **Per-demo GUI executables** (virtualbasys_counter, virtualbasys_stopwatch)
  apply the one-verilated-lib rule to demos; interim until the phase-2
  `virtualbasys run` CLI.
- **sim_* libraries verilate with OPT_FAST/OPT_GLOBAL -O2**: the default
  (empty) build type left generated code at -O0 — measured 3.3 vs 20
  Mcycles/s, i.e. a ~90 s vs ~15 s stopwatch golden. Honest perf baseline:
  ~20 Mcycles/s = 0.2x real-time under --public-flat-rw + --trace-vcd — R1's
  "examples exceed real-time" guide is NOT currently met; the phase-2 speed
  indicator starts from this number (docs/versions.md).
- **VCD acceptance = in-test structural validation + Surfer headless gate.**
  Surfer (`surfer server --file X`) exits 1 only on HEADER-corrupt input; on
  body corruption it logs "Loaded header", then a loader thread panics
  ("Surfer crashed due to a panic") WITHOUT exiting — so the gate must wait
  for the "Loaded body" line and treat panic banners as failure (it does).
  Homebrew's gtkwave cask is DISABLED (upstream discontinued, 2025-10-29) —
  never plan around brew-installed GTKWave; manual GTKWave verification uses
  the GitHub-release app.
- **Example RTL counter idiom**: full 32-bit counters compared directly
  against integer localparams with sized increments — the verified
  -Wall-clean Verilog-2005 form ($clog2-sized counters trip WIDTHEXPAND;
  width-derived localparams trip WIDTHTRUNC). DIGIT_DWELL = 25000 (250 us per
  digit, 1 ms full refresh) is what the Basys 3 RM means by "~1 kHz refresh".

Added in milestone 1.5 (UART):

- **UART events joined the single structured log — header v=2** (R3 defines
  ONE log; the round-trip reads in order in one stream). Goldens regenerated;
  the stopwatch diff was verified header-lines-only before commit.
- **TX is a grid-observed sibling observer, RX is exact-cycle**: UartTxDecoder
  samples the line at grid crossings (safe while one bit >= ~4 chunks, i.e.
  down to ~25 kBd at the 1000-cycle chunk); UartRxDriver bit edges are poked
  at precise cycles — BoardModel::tick additionally splits stepping at RX
  edges. Log events are byte-level (start-bit stamp for RX, stop-bit grid
  crossing for TX); per-bit pokes are not logged.
- **UART_RX idles HIGH**: BoardModel pokes it high at construction — the
  2-state zero-init would otherwise present a spurious start bit at power-on.
  Designs must still btnC-reset their RX synchronizers (R6): the engine
  settles t=0 with all inputs low before the poke lands.
- **Echo designs need a one-deep RX->TX pending buffer**: a TX frame is busy
  10*BIT+1 cycles while back-to-back input arrives every 10*BIT — without the
  buffer every byte after the first lands on the busy-clearing edge and drops
  (uart_echo.v documents this).
- **The RX driver owns the line through the stop bit's FULL duration**: a
  send() issued during the stop tail defers its start bit to frameStart +
  10*BIT rather than truncating the frame in flight (truncation silently
  dropped the previous byte — panel-found, regression-tested). Overdue RX
  edges apply late-but-deterministically if a caller advances the engine
  around BoardModel::tick.
- **`--send CYCLE:TEXT`** scripts UART input through the same
  BoardModel::sendUartText path as the GUI console widget.
- **Perf decomposition is in docs/versions.md** (bare 29.8 / +public-flat-rw
  21.6 / +trace-compiled-but-off 21.6 Mcycles/s): trace support is FREE when
  off; --public-flat-rw costs ~28%; the 0.3x bare ceiling is Verilator
  per-eval overhead at two evals/cycle — phase-2 levers ranked there. The
  GTKWave half of the 1.4 VCD criterion is closed (attestation recorded in
  docs/versions.md, 2026-07-15).

## Explicit non-goals

- No synthesis tool of our own (Yosys handles it).
- No bitstream generation, no place-and-route, no real static timing analysis.
- No gate-delay/analog accuracy; cycle-accurate only.
- No true multi-clock async CDC simulation (single master clock + enables).
- No Windows/Linux GUI support until the Mac version is solid (keep core portable).
