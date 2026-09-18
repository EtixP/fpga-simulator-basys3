# Pre-migration performance baseline — 2026-09-18

Measured after correctness fixes, before Qt work, implementation commit **996cad1**.
Raw samples: [pre_migration_performance.csv](pre_migration_performance.csv).

| Example | Median Mcycles/s | Range Mcycles/s (3 runs) | Real-time multiplier | VGA frames/s |
|---|---:|---:|---:|---:|
| Counter | 21.838 | 21.758–21.994 | 0.2184x | — |
| Stopwatch | 16.520 | 16.518–16.932 | 0.1652x | — |
| UART echo | 18.590 | 18.527–18.767 | 0.1859x | — |
| VGA pattern | 14.910 | 14.882–14.914 | 0.1491x | 8.875 |

## Environment and method
- Apple M1 Pro, 10 CPU cores, 32 GiB RAM; Darwin 25.5.0 arm64.
- Apple clang 21.0.0 (clang-2100.1.1.101), CMake 4.0.0, Verilator 5.050.
- Normal GUI-enabled build, empty CMAKE_BUILD_TYPE; existing engine/board hot paths
  and generated models retain their explicit -O2. Sanitizers are **off** here.
- Same `--public-flat-rw --trace-vcd --timescale 1ns/1ns` model flags as tests.
- Each run constructs a fresh engine/binding/BoardModel, holds reset for 16 cycles,
  warms for 2,000,000 cycles, then times exactly **20,160,000** BoardModel cycles.
  Construction, reset, warmup and output printing are outside the measured interval.
- Counter switches are 0xF; stopwatch is started through its debounced button;
  UART has 512 queued 'U' bytes; VGA runs its full capture/assembler path.
- Three sequential samples per design, after builds/tests/GUI processes finished.
  No core pinning or artificial timing smoothing. Report median and full observed range.
- VCD dumping and structured-log collection disabled; normal peripheral observation
  and UART byte collection remain enabled. No SDL/Metal presentation in timed runs.
- Each VGA sample completes exactly 12 frames. Frames/s uses completed-frame count
  divided by wall duration; it is not GUI render rate. Multiplier = cycles/s / 100M.

## Reproduce

```sh
cmake -B build -DVB_BUILD_BENCHMARKS=ON
cmake --build build -j 8
for design in counter stopwatch uart_echo vga_pattern; do
  "build/bench/benchmark_${design}" 20160000 3
done
```

Use the normal build, with no sanitizer flags and no competing tests or builds.
CSV reports each run separately; no hidden best-case selection. Compare the same
workloads/flags on the same machine when assessing Qt adapter or VGA overhead.

## Interpretation

The 100 MHz real-time guide remains unmet; the measured simulation runs at roughly
0.15–0.22x real-time. VGA remains near the historical headless ~15 Mcycles/s/~9 fps
record in `versions.md`; the current 8.875 fps is below the >=10 fps guide. That
historical comparison is approximate, not a controlled before/after benchmark.

Correctness and deterministic virtual time pass independently of throughput. Future
pacing must show measured speed honestly. Measure GUI presentation separately at M6;
this baseline includes no claim about Qt performance or wall-clock 60 fps rendering.
