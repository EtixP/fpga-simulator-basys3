# Scripted runs in the Qt launchers

M8 gave the Qt launchers the scripted-run options of the earlier ImGui demos,
with the same syntax, the same messages and the same simulation results, before
removing that frontend. A
scripted run is reproducible: its inputs happen at exact virtual cycles, so the
same arguments always produce the same structured log, however fast the window
runs.

```sh
./build/qt/src/qt/virtualbasys_qt_stopwatch --frames 300 \
  --at 100000:BTNU=1 --at 1300000:BTNU=0 --log stopwatch.log --screenshot stopwatch.png
./build/qt/src/qt/virtualbasys_qt_uart --frames 12 --send 100000:hello --log uart.log
./build/qt/src/qt/virtualbasys_qt --switches 0101        # runs until you close it
```

## Options

| Option | Meaning |
| --- | --- |
| `--xdc PATH` | Use these constraints instead of the built-in ones; an unreadable or empty path is an error. Alone, it does not script the run. |
| `--frames N` | End the run after N frames, then write the screenshot and log and exit. A frame is 100,000 cycles; in the VGA launcher 1,700,000, a little more than one 1,680,000-cycle VGA frame. |
| `--at CYCLE:NAME=V` | Set SW0..SW15 or BTNC/BTNU/BTNL/BTNR/BTND to 0 or 1 at a cycle. Repeatable. |
| `--switches BITS` | Turn switches on at cycle 0; the rightmost digit is SW0. |
| `--send CYCLE:TEXT` | Queue TEXT's bytes to the design's UART receive line at a cycle. Repeatable. |
| `--log FILE` | Record the structured log from cycle 0 and write it when the app exits, including by Ctrl-C or SIGTERM. |
| `--screenshot FILE` | Save the window when `--frames` ends the run, as displayed (a tooltip under the pointer included). The file suffix picks the format; a name without a suffix Qt can write gets BMP, with a warning if it has another suffix. `--frames 0` saves nothing. |

Any option except `--xdc` makes the run scripted. Launches without them are
interactive, as before: they open paused, and only the UART and VGA launchers
apply one Reset first.

## Semantics

These are the frozen rules in the
[simulator invariants](simulator_invariants.md#scripted-frontend-startup),
implemented once in `src/script/` and used by the launchers and tests.

- **Startup.** Logging starts first. A scripted run then holds BTNC for cycles
  0–15 while its inputs apply at their own cycles; a scripted BTNC event in that
  window takes over the reset. `--frames 0` applies nothing and advances nothing.
- **Order.** Inputs at the same cycle apply in argument order, `--switches`
  included. Switches and buttons at a cycle apply before sends at that cycle.
- **End.** A finite run ends at cycle 16 + N × frame length (0 for N = 0).
  Every advance is clipped there, and Run, Step and Reset are then disabled.
  The footer shows "Scripted run · ends at cycle N", then "Scripted run complete".
- **While it runs.** A scripted run starts running by itself, in turbo unless
  `--realtime` is given. You can pause, step, reset and use the board. Every
  advance, whether a Run batch of any size, Step or Reset, goes through the same
  scheduler, so script events still apply at their exact cycles. A scripted
  BTNC event during a Reset pulse owns BTNC afterwards, as during the startup
  reset; otherwise Reset restores BTNC's earlier level.
- **Logs view.** With `--log`, the Logs tab can record at the same time. It
  reads the board's log without clearing it, so the file stays complete.
- **UART terminal.** A scripted send appears as an RX row, stamped with its
  first start bit and placed at the cycle the script sent it, like a typed send.
  Unlike typed text, it has no 4,096-byte queue limit or UTF-8 check, as in the
  ImGui demos.
- **Messages and exit codes.** Argument errors are the ImGui demos' messages,
  such as `error: --frames '-7': expected a non-negative integer`, and exit 1
  before any window opens. Values follow their option as the next argument: `--frames=1` and stray words, such as unquoted `--send` text, are
  errors. Script events past the end of a run, or not reached before the window
  closes, are reported as warnings. Inputs to unbound switches or buttons, and
  sends to a design without a receive pin, are ignored, as in the ImGui demos.
  A screenshot or log that cannot be written is an error and exits 1.

## Differences from the ImGui demos (removed in M8)

The simulation results are identical. Only presentation differs:

- Launched without scripted options, the ImGui demos started running at once,
  all four after a startup reset. Qt launches open paused; only the UART and VGA
  launchers apply one Reset first.
- The ImGui demos advanced one frame per display refresh; Qt runs scripted
  runs in turbo. Only wall-clock time differs.
- Qt saves a screenshot of its own window, in the format the suffix names; the
  ImGui demos always wrote BMP.
- `--screenshot` without `--frames` prints a warning; the ImGui demos ignored it.
- `--preview` and `--smoke-test` cannot be combined with scripted runs.
- Unknown options are reported by Qt's option parser.
- A run longer than the 64-bit cycle counter can reach (about 1.8 × 10¹⁴ frames
  of 100,000 cycles, 1.1 × 10¹³ in the VGA launcher) is rejected at startup; the
  ImGui demos accepted it and ran until closed.
- A `--frames` run ended early, by Ctrl-C or by closing the window, warns that
  it saved no screenshot.

## Verification

- **`qt_script_cli_<design>`**, for all four launchers, runs the real launcher
  offscreen as a separate process. The oracle replays the same arguments
  in-process with the ImGui demos' loop: the startup, then one scripted advance
  per frame. Log files must be byte-identical. Cases:
  - counter: switch presets, button holds, an event past the end, a scripted
    BTNC taking over the reset, same-cycle argument order both ways, zero
    frames, other constraints and their diagnostics, argument errors (stray
    words, `--frames=1`, an empty `--xdc`), unwritable logs and screenshots,
    screenshot formats, and SIGTERM ending an unlimited run with its log;
  - stopwatch: 0.3 s with start, lap and stop presses while the display counts;
  - uart_echo: two queued sends and their echoes;
  - vga_pattern: two VGA frames, pinned by events at and after the last cycle,
    and the screenshot.
- **`qt_script_run_<design>`** drives the controller in-process against the
  shared scheduler:
  - counter: identical logs for Run batches of 1, 997, 100,000 and 1,000,000
    cycles; Step and Reset with script events inside the reset pulses,
    including scripted BTNC changes that own BTNC afterwards; the end clip and a
    single `runFinished`; zero-frame, unlimited and failed runs; the retained
    log while the Logs view records and clears;
  - uart_echo: the terminal rows are identical whether the adapter refreshes
    every 997 cycles or rarely; queued byte counts; Clear drops unshown
    scripted rows; a Reset cut short by the end reports its real length.
- **`qt_simulation_ui`** and **`qt_uart_ui`** check a scripted run in the real
  window: the footer, the disabled (and dimmed) controls, the title, and the
  terminal rows of scripted sends.
- **ImGui executables, compared directly.** The real ImGui demos and the Qt
  launchers, given the same arguments, wrote byte-identical logs and identical
  warnings in seven scenarios: counter (2), stopwatch (40 and 300 frames), UART
  echo, VGA and zero frames. Both screenshots of the VGA run show the same
  frame 0. An independent review repeated this for 123 runs, 82 of them
  randomized scripts, with identical logs, warnings and exit codes.
- **Removal review.** Before the ImGui frontend was deleted, two further
  reviewers compared 187 argument sets across the four designs, 75 of them
  randomized: the launchers without the ImGui code wrote the same logs, exit
  codes and messages as before and as the ImGui executables. The test
  definitions were unchanged (44 with Qt, 18 without).
