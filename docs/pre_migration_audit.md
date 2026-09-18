# Pre-migration correctness audit — 2026-09-18

P0–P3 audit of baseline ec17bd1 on macOS arm64, Apple M1 Pro, Verilator 5.050.
No Qt work or simulator rewrite. Existing golden files were not changed.

## Method and baseline
Read implementation, RTL, tests and historical decisions before changing production
code. The original GUI-enabled build succeeded. The original suite passed 10/11;
`stopwatch_vcd_surfer` timed out at 120 seconds because inherited `RUST_LOG=warn`
suppressed its info-level verdict. Rerunning with `RUST_LOG=info` passed in 0.08 s.

Primary subsystem audits used independent synthetic waveforms and real Verilator
fixtures, not just golden agreement. A fresh P2 verifier returned **FAIL**, confirmed
the initial findings, and added coincident VGA sync and sustained UART throughput
cases. Confirmed bugs were reproduced before correction. Two fresh P3 verifiers
subsequently returned **PASS**, with no remaining blocking or minor findings.

## Bugs found and fixed
| ID | Reproduction and consequence | Correction / durable regression |
|---|---|---|
| F1 | Quoted XDC names retain quotes; continuations/multiline dictionaries/semicolon commands lose constraints; accepted literals fail roundtrip; invalid clock periods accepted | Bounded literal tokenizer, escaped serialization, numeric validation; `test_xdc` |
| F2 | `[7:4]` bit 4 rejected; `[0:3]` bit 0 drives the wrong packed bit; hierarchical internals accepted as get_ports | Preserve declared signed range/scalar distinction, convert index to offset, bind top-level ports only; `binding_ranges` real RTL |
| F3 | Inverted Vsync shifts row 2 to row 0 while reporting healthy; simultaneous H/V falls shift by one row | Vsync duty validation and time-relative row reconstruction; independent `vga_contract` plus existing pixel golden |
| F4 | A single 1000-cycle low pulse followed by idle emits fabricated 0xFF | Validate candidate start midpoint; malformed-start/stop and recovery in `peripheral_contract` |
| F5 | Script transitions at cycles 0 and 8 both occur at 16 after demo startup reset | Pure C++ scheduler services reset/scripts together from cycle zero; per-edge `gui_runner` oracle |
| F6 | Required viewer waits forever for a suppressed success log | Set child logging, private ephemeral port, bounded reads and child reaping; `stopwatch_vcd_harness` covers success/panic/EOF/hang/unavailable |
| F7 | TX needs 10*BIT+1 cycles while continuous RX arrives every 10*BIT; finite buffering eventually overflows | Seamless handoff after full stop bit, preserve simultaneous receive/consume; `uart_stream` uses same RTL with BIT=17 |
| F8 | Button(99) indexes out of bounds (ASan stack-buffer-overflow) | Guard public button access consistently with switches; invalid 5/99/UINT32_MAX in `peripheral_contract` |

F7 baseline reduced-divisor probe returned 995/1000 bytes, first missing byte 170;
fixed probe returned 1000/1000. Durable regression checks 2000 bytes and exact cadence.
An independent reviewer compared every raw TX cycle against an ideal waveform:
2000 bytes at BIT=17 and 128 bytes at the default BIT=10417, including full stop bits.
A proposed 11-billion-cycle default-baud probe was not completed; no result is claimed.

Review refinements also fixed declared-index duplicate diagnostics, wildcard/newline
multi-port rejection and braced option interpretation. Clock falling edges may precede
rising edges (`-waveform {7 2}` is valid); tests preserve that AMD-documented behavior.
The seven-segment >=2-chunk capture condition is sufficient, not an iff claim.

## Coverage after audit
| Subsystem | Before | After | Independent evidence |
|---|---|---|---|
| Engine cycles/init/poke/peek | ADEQUATE | STRONG | Both edges, step0/1/N, million-cycle partitions, declaration/readmem init, async reset, derived clock, 16/32/64-bit values |
| Capture/trace/context lifetime | ADEQUATE | STRONG | Default/optimized sample and VCD equivalence; independent exact timestamps; trace retry/pause; surviving engines after non-LIFO destruction/constructor failure |
| Switches/buttons/LED | ADEQUATE | STRONG | Exact recorded input edges, rapid/grid-boundary changes, invalid inputs, real counter/reset |
| Seven-segment | ADEQUATE | STRONG | Existing 300M-cycle stopwatch/count/debounce/mux/aliasing tests; direct polarity, all-digit ordering, decode and inclusive-expiry boundaries |
| UART | ADEQUATE | STRONG | All 256 bytes at every grid phase 0..999, divisors 10417/4000; exact RX bit edges; stop-tail, malformed frames, recovery, sustained stream |
| VGA | WEAK | STRONG for supported mode | Independent gradients/bit lanes through bit63, cpp1..5, two sync origins, malformed timing/recovery; existing frame stamp and actual BoardModel partition/pixel equality |
| Structured log/chunks | ADEQUATE | STRONG | Byte-exact existing goldens; irregular partitions; SSEG/DP/LED/TX/RX same-stamp ordering; enable/disable/clear; tick0 |
| XDC/default/binding | ADEQUATE, range cases MISSING | STRONG for documented subset | 105 pristine/default pins; declared ascending/nonzero/negative/one-bit vectors; literal syntax, roundtrip, unsupported forms, duplicate/index diagnostics |
| GUI script execution | MISSING | STRONG for scheduler | Recorded every edge around 0/8/16 and frame endpoints; reset ownership, UART, stable order, chunk invariance, zero frames |

The original VGA expected image follows the RTL pattern specification; its absolute
color/orientation anchors are useful but insufficient alone. New independently
constructed gradients exercise intermediate channel bits, alternate timing and
scrambled lanes. New board/engine partition tests avoid assuming the capture path.

## Verification evidence
- GUI-enabled normal build and full suite: 18/18 pass, including Surfer.
- Fresh independent verifier A full suite: 18/18; independent raw-wire UART oracle.
- Clean headless Release build with AddressSanitizer + UndefinedBehaviorSanitizer:
  18/18 pass (final narrow parser corrections revalidated in this configuration).
- Verifier A: standalone sanitizer peripheral/GUI tests pass.
- Verifier B: sanitizer VGA phase sweep (18 cpp/phase combinations); 20,000 literal
  serialization cases; native Tcl interpretation comparison for 1000 values pass.
- All four legacy GUI demos completed finite smoke runs; startup log stamps 0/8
  were exact, GUI UART echoed Hi, and zero-frame log contained only its header.
- Existing v2 UART/stopwatch golden files unchanged; VGA frame stamp 4,928,013 unchanged.
- Commands: `cmake -B build -DVB_BUILD_BENCHMARKS=ON`, `cmake --build build -j 8`,
  `ctest --test-dir build --output-on-failure`. Sanitizer build uses VB_BUILD_GUI=OFF,
  CMAKE_BUILD_TYPE=Release, CXX/link flags `-fsanitize=address,undefined` and
  `-fno-omit-frame-pointer`; halt_on_error=1 for both sanitizers.

## Remaining limits and migration decisions
These are supported-scope limits, not unresolved discovered correctness bugs:
- Two-state, one master clock with RTL-derived clocks/enables; no true async CDC,
  internal tristates, >64-bit/unpacked/multidimensional packed signal access.
- Literal XDC subset: no executable Tcl, variable/command evaluation, numeric/Unicode
  escapes, wildcard/range/multi-port targets. Unsupported forms warn/skip.
- Slow observations remain 1000-cycle grid quantized; narrow output pulses may alias.
- Board UART is fixed at ~9600 baud; byte/log vectors currently grow without a cap.
- VGA is fixed 640x480/800x525, active-low sync, integer cycles/pixel. All 14 pins and
  <=64 bits of distinct whole watched ports are required; pulse/porch checks are not
  exhaustive. Capture-slot frame stamps intentionally lag post-edge now() by one.
- No concurrent engine/BoardModel access. Future Qt worker ownership/snapshots and
  board-level reset/inspection/trace APIs require an explicit design.
- This session rechecked Surfer, not the previously recorded manual GTKWave GUI gate.
- Performance is measured headlessly in `pre_migration_performance.md`; real-time
  speed is not promised. Legacy frontend remains until independently verified parity.

## Primary references
Installed Verilator 5.050 headers/runtime/generated models are the version-specific
API evidence; [upstream integration](https://verilator.org/guide/latest/connecting.html)
documents evaluation/finalization. Literal syntax was checked against
[Tcl syntax](https://www.tcl-lang.org/man/tcl8.6/TclCmd/Tcl.htm) and
[AMD create_clock](https://docs.amd.com/r/en-US/ug835-vivado-tcl-commands/create_clock).
Default pins match the checked-in pristine
[Digilent master](https://github.com/Digilent/digilent-xdc/blob/master/Basys-3-Master.xdc).
