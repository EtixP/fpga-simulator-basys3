# Qt inspector and event log

M7 adds two views of the running design:

- the **Inspector** pane lists the design's signals and their values;
- the **Logs** tab records the board's cycle-stamped structured log.

Both show state that `BoardModel` already exposes: the simulator reads values and
produces events, and Qt only displays them, filters them and follows new rows.
QML calls no C++ method per signal or per row.

## Inspector

The Inspector pane lists every supported top-level port of the loaded design, in
the engine's name order, followed by your watches. Each row shows:

- the name and declared range, for example `led[15:0]`;
- the kind: input, output, clock or watch. The clock is the port that the
  constraints bind to the 100 MHz oscillator pin (`CLK100`);
- the value, as `0`/`1` for one bit and zero-padded hex otherwise, with its
  unsigned decimal value;
- the board pins bound to the port, for example `LED0–LED15`.

Ports that no pin constrains show no pins.

**Snapshots.** All values in a snapshot are read together at one virtual cycle,
shown as "Values at cycle N". Hover over it to see the virtual time. Rows whose
value differs from the previous snapshot are highlighted. Every refresh takes a
snapshot, including one at the same cycle after a switch or button change or a
new watch. After a step, the highlight therefore shows what the step changed
until the next input, watch or step. While running, the snapshot follows the controller's roughly 16 ms
refreshes, so highlights come and go quickly.

**The clock.** The 100 MHz master clock reads 0 before the first rising edge.
After that it reads 1, because every snapshot is taken after a rising edge.

**Watches.** Type a hierarchical RTL name rooted at the top module, such as
`counter.count` or `stopwatch.s0`, and press Return or **Watch**. The row is read
and updated like a port. Remove it with its **×** button. Watches are read-only.
They are best effort, as the engine contract describes: a future gate-level
engine may not have a signal that synthesis renamed or removed. A watch is
rejected, with a message, when the name:

- is a plain name (ports are already listed);
- is already listed;
- is unknown;
- is a memory or wider than 64 bits;
- would exceed 32 watches.

**Filter.** "Filter signals" shows the rows whose name contains the text, ignoring
case, in the same order.

**Keyboard.** Tab into the list to scroll it with the arrow keys, Page Up/Down,
Home and End. Tab and Shift+Tab then reach each watch's **×** button in row
order, scrolling it into view; Space removes the watch and keeps focus in the
list.

## Event log

The Logs tab shows the board's structured log (R3): LED and seven-segment output
changes observed on the 1,000-cycle grid, UART bytes and framing errors, and
exact-cycle switch and button changes. Each row has the event's virtual time,
cycle, a category tag (DISP, LED, UART, IN) and the log's own text.

**Recording.** Collection is opt-in, as in the simulator, and starts only when you
press **Record**. While recording, the log view owns the board's structured log:

- Recording first discards lines logged before it started.
- Every refresh moves new lines into the view and clears the board's copy, so
  the simulator's log memory stays small however long you record.
- **Stop** moves the remaining lines into the view and restores the board's
  previous logging setting.
- Notice rows mark where recording started and stopped.
- Switching to another output tab does not stop recording. If the adapter is
  destroyed while recording, which happens only when the app exits, lines not
  yet moved are discarded and the previous setting is restored.

Recording never advances time, and rows keep the log's emission order and
stamps. Refresh rate and wall-clock time never change them.

**Filtering.** The Display, LEDs, UART and Inputs check boxes show or hide those
categories. The text filter matches event text, ignoring case, or cycle numbers.
Start and stop notices ignore the check boxes, but the text filter applies to
them. The status line reads "N of M shown".

**Scrollback.** The view keeps the newest **10,000 events**; older ones are
discarded, and a line above the list says how many. Following, "Latest ↓",
scrolling back by wheel or scroll bar, and keyboard scrolling after tabbing into
the list work as in the [UART terminal](qt_uart.md). Following continues at the
10,000-event limit. **Clear** empties the view, including lines logged but not
yet shown, and resets the discarded count; recording continues.

## Architecture

`BoardModel` gained read-only pass-throughs, as the roadmap requires for
inspection APIs:

- `designPorts()`, `findSignal()`, `signalInfo()` and `readSignal()`, using the
  existing `SimEngine` ports, lookup, info and peek;
- `logEnabled()`;
- `PinBinding::resources()`, which lists which board resources landed where.

These change no behaviour. `readSignal` keeps peek semantics: pending inputs
settle, and neither the clock nor time advances.

`BoardAdapter` builds the inspector rows once and keeps their signal handles in
C++. On each refresh it reads every value in one pass and drains the log if
recording. As for every other model, it reads everything before notifying any
view. Only changed row ranges are reported, and log rows are inserted in one
block per refresh.

The inspector creates every row, not just those near the view: there are at most
the design's ports plus 32 watches. A `ListView` creates rows lazily, and Tab
would then skip the remove buttons of rows not yet created. A `Repeater` keeps
its rows in model order, so Tab and Shift+Tab follow row order even after
filtering. Rows out of view keep their last text and catch up as soon as they
scroll into view, so a refresh lays out only the visible rows. The event log
keeps a `ListView`: it holds up to 10,000 rows and has no focusable controls in
its rows.

QML binds to C++ filter proxies (`inspectorView`, `eventLogView`). Watches,
recording and clearing are `BoardAdapter` methods with the usual thread and
re-entry guards. QML never sees an engine, binding or signal handle.

The output panel is a `StackLayout`, so every page already has its size and
position before it is first shown. Without it, a page shown for the first time
would briefly sit over the tab bar until the next layout pass.

## Performance

```sh
cmake -S . -B build/qt -DVB_BUILD_QT_GUI=ON -DVB_BUILD_BENCHMARKS=ON
cmake --build build/qt --target benchmark_qt_refresh_counter benchmark_qt_control_counter -j 8
./build/qt/bench/benchmark_qt_refresh_counter 20000000 3   # also _stopwatch, _uart_echo, _vga_pattern
./build/qt/bench/benchmark_qt_control_counter 20000000 3   # also _stopwatch
```

`benchmark_qt_refresh_<design>` runs 100,000-cycle batches four ways:

- **tick:** no adapter work;
- **refresh:** an adapter refresh, and so an inspector snapshot, after every batch;
- **board_logging:** the board's own logging alone;
- **refresh_logging:** refresh while the event log records.

The running app refreshes at most every ~16 ms, so one refresh per batch is a
worst case. The benchmark measures the adapter alone, with no rendered view. The
Verilated designs are built with `-O2` as always, but the adapter uses the
project's build type, which was empty (unoptimized) here; the adapter costs are
therefore upper bounds. Measured on 2026-09-24 with macOS 26.5.2 arm64 and
Qt 6.11.2, three runs of 20 million cycles each; the table shows medians.

| Design | Tick, Mcycles/s | Refresh | Board logging | Recording | Events recorded |
| --- | ---: | ---: | ---: | ---: | ---: |
| Counter (switches on) | 21.003 | +1.5% | +0.3% | −8.3% | 158,776 |
| Stopwatch | 17.828 | +1.6% | +0.8% | +0.7% | 23 |
| UART echo | 18.899 | −2.0% | 0.0% | −1.4% | 385 |
| VGA pattern | 14.108 | 0.0% | −0.1% | −0.6% | 1 |

Percentages are relative to Tick; positive means faster. [Raw
samples](qt_inspector_log_refresh.csv).

- **Inspector.** The snapshot has no measurable cost: the differences run
  from −2.0% to +1.6%, in both directions, like run-to-run noise.
- **Recording.** Recording is cheap unless the design emits many events.
  - The counter with all switches on changes several LEDs at almost every
    observation: about 8 events per 1,000 cycles, or roughly 160,000 events per
    wall-clock second at this speed. That costs 8.3%.
  - Profiling shows the board's own string formatting costs about 1%. The rest
    is moving, parsing and storing events in the view.
  - Two measured fixes are already in: reserving space for each batch, and
    skipping filter checks when nothing is filtered.
- **Full window.** Measured in the same session with the always-visible
  inspector ([raw samples](qt_inspector_log_control.csv)):

  | Design | Direct, Mcycles/s | Controller | Rendered turbo | Rendered 1× |
  | --- | ---: | ---: | ---: | ---: |
  | Counter | 21.583 | 17.091 | 16.681 | 16.578 |
  | Stopwatch | 18.594 | 15.169 | 14.859 | 14.926 |

  M5's same-path counter result was 21.413 / 17.294 / 16.836 / 16.630. The
  stopwatch window is 20% below direct, against 25% in M4. Nothing points to a
  regression.
- **Many changing watches.** A worst case for the inspector: a synthetic design
  with 5 ports and 32 watches whose values all change at every refresh, fast
  enough that refresh cost shows. Rendered full window, turbo, 4 s runs,
  offscreen, 4 interleaved runs each, medians:

  | Inspector rows | Mcycles/s |
  | --- | ---: |
  | `ListView` (first M7 version) | 90.38 |
  | `Repeater`, every row updating | 85.27 |
  | `Repeater`, rows out of view keep their text (shipped) | 93.36 |

  Updating every row cost about 6%; the shipped version is faster than the
  `ListView`. The reviewer's native runs of the first two agreed (90.7 / 84.5).
  These numbers come from a review probe that is not in the repository.

## Verification

```sh
ctest --test-dir build/qt -R '^qt_inspector_' --output-on-failure
```

- **`qt_inspector_log`** tests the adapter and models with a synthetic design
  whose values follow simple arithmetic. Its expected event rows are a second
  board's own structured log under the same stimulus. It covers:
  - rows, kinds, declared ranges and pin summaries;
  - batched change ranges, all read at one cycle;
  - watches and each rejection message, up to the 32-watch limit;
  - the name filter;
  - recorded rows equal to the independent log at three refresh rates, with the
    board's copy kept bounded;
  - Stop and Clear taking lines logged since the last refresh, and Clear or
    destruction after Stop leaving the board owner's lines alone;
  - restoring the previous logging setting, including when the adapter is
    destroyed, without leaving undrained lines;
  - pin summaries that collapse only for consecutive bits and pins;
  - categories, parsing (including out-of-range cycles) and the event filter;
  - the 10,000-event limit;
  - guards against other threads and re-entry.
- **`qt_inspector_ui`** runs the real counter RTL through clicks, typing and wheel
  events:
  - values that follow the RTL's arithmetic, with the rendered highlight;
  - keyboard watches and their errors;
  - the filter;
  - events equal to another counter board's log under the same stimulus;
  - category and text filters, Stop and Clear, and each empty-state message;
  - following past the limit, including batches that keep the row count
    constant, and scrolling back and forth by keyboard, scroll bar and wheel;
  - in a short inspector panel with 5 ports and 32 watches: the watch limit;
    wrapped messages for names without spaces; every row in view, including
    partly visible rows and rows a taller window reveals, showing the snapshot's
    value, decimal and highlight; rows out of view left alone until they scroll
    in, then current at once; keyboard scrolling that ends exactly at the end; Tab and
    Shift+Tab through every remove button in row order, each kept in view;
    focus staying in the list after a keyboard removal, and no outline after a
    mouse removal;
  - focus outlines on both lists;
  - the minimum window and the unavailable states.
- **Existing tests.** Two assertions that expected the old "Log view unavailable"
  placeholder for connected boards now check the log view. The UART UI test,
  which clicks Logs and then UART with no frame in between, catches the
  output-panel overlap described under Architecture.
- **Mutation checks.** Fifty-three deliberate faults were each introduced
  temporarily; each made a test fail:
  - ungrouped change notifications; a highlight that never clears; a watch not
    read when added;
  - a missing clock tag; pin summaries that never collapse;
  - the board log not cleared, or earlier lines kept; the logging setting not
    restored;
  - an off-by-one event limit; notices hidden by categories; a filter shortcut
    that ignores categories; header lines parsed as events; plain names accepted
    as watches;
  - the output panel without its `StackLayout`; the log not following at the
    limit; the text and signal filters not wired; changed rows not highlighted;
  - Stop or Clear ignoring lines not yet refreshed; undrained lines left on the
    board when the adapter is destroyed; Clear or destruction after Stop wiping
    the owner's log; pin summaries ignoring bit order; cycle stamps beyond 64
    bits accepted; Clear keeping the discarded count;
  - the inspector ignoring End, Up or Page Up, or scrolling past the end; a
    focused remove button left out of view; remove buttons that Tab cannot
    reach; focus lost after a keyboard removal, or the outline shown after a
    mouse removal; rows scrolled into view not catching up, or judged in view
    wrongly (only whole rows, or against a fixed height); decimals or
    highlights never updating; rows out of view updated anyway; Watch enabled at
    the limit; messages that do not wrap; missing
    focus outlines;
  - the log ignoring Up or Page Up; End, Page Down, the scroll bar or the wheel
    not resuming following; the wrong empty-state message.

  The inspector's former `ListView` also fails the Tab-order test: from a list
  showing only ports, Tab reached none of the 32 remove buttons.

## Limitations

- Internal signals cannot be listed automatically, because the engine interface
  enumerates only ports. Watches need the RTL name, which you know from your own
  design.
- Values are snapshots taken at refreshes, not a waveform. A signal can change
  and change back between two snapshots. The structured log and VCD tracing are
  the exact records.
- Designs that emit many events cost measurable throughput while recording (see
  Performance). Recording is off until you start it.
- Conditional breakpoints ("pause when X == V") are not part of M7.
