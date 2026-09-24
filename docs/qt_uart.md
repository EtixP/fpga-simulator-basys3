# Qt UART terminal

M5 adds a UART terminal to the Qt frontend's **UART** output tab. It shows what a
design transmits on RsTx and sends host text to RsRx, using the simulator's
existing 8N1, 9600-baud UART model. Every byte value, bit time and log line comes
from that model; the terminal only groups bytes into rows and displays them.

```sh
./build/qt/src/qt/virtualbasys_qt_uart
```

`virtualbasys_qt_uart` runs the built-in `examples/uart_echo.v`, which sends
back every byte it receives. Type text, press Return or **Send**, then **Run** or
**Step**. The echo appears about one frame (104,170 cycles) after each byte
arrives. Designs that bind a USB-UART pin open on the UART tab; the counter and
stopwatch do not bind these pins, so their UART tab says so.

## Startup reset

The counter and stopwatch launchers still open at cycle zero. The UART launcher
opens at **cycle 16** because it applies the Reset control once before the window
appears: BTNC is held for 16 cycles, the same as clicking **Reset**. Without that
pulse, `uart_echo`'s two-flop receiver synchronizer starts low (registers
without an initial value start at zero), sees a false start bit and echoes
`0xFF`. Design rule R6 says every design needs an explicit btnC reset, and the
legacy demos apply the same startup pulse. The terminal's first row records it.

## Reading the terminal

Each row shows the virtual time and cycle of its first byte, a direction tag and
the row's text. TX and RX use the design's naming, which the structured log also
uses:

| Tag | Meaning | Stamp |
| --- | --- | --- |
| **TX** | Bytes the design sent on RsTx (A18) | The observation-grid cycle at which the stop bit was sampled. This is the same stamp as the `UART TX` log line, 39–1,038 cycles after the ideal stop-bit center. |
| **RX** | Text sent from this terminal to RsRx (B18) | The cycle at which the first start bit is driven. This is the same stamp as the `UART RX` log line. While paused it can be in the future, because queued bytes go out back to back. |
| `··` | A notice: a completed Reset pulse, or one TX framing error | The pulse start, or that error's stop-bit sample (its `UART TX framing error` log stamp) |

Hover over a row to see its byte count, its first and last cycles and its exact
bytes in hex. **Hex** shows every row's bytes in hexadecimal.

Rows follow these rules:

- **Row breaks.** A row ends after a newline byte (`0x0A`) or at 128 bytes. A row
  also ends when a row of the other direction or a notice is added, and when the
  scrollback is cleared.
- **Sends.** Each send starts a new row.
- **Ongoing output.** TX output without a newline continues its row across
  refreshes and Pause/Run. How often the screen refreshes never changes the rows.
- **Displayed text.** A trailing CR+LF or LF is hidden from the text, but not
  from the hex view. Printable ASCII is shown as-is. Tab and CR are shown as
  `\t` and `\r`, and all other bytes as `\xHH`. A literal backslash is shown
  unchanged; use Hex when that could be ambiguous. ANSI escape sequences are
  shown, not interpreted.

## Sending

The input field sends its text as UTF-8 bytes; a non-ASCII character becomes
several bytes. The line-ending menu adds nothing (the default), LF, CR or CR+LF.
Sending never advances time: bytes wait until **Run** or **Step** advances the
simulation. A send's row appears immediately, so a send queued while paused can
show a future stamp above output and notices added later with earlier stamps.
Output and notices are always ordered by stamp among themselves. Text that
UTF-8 cannot represent, such as an unpaired surrogate, is rejected whole.

The status line shows session totals for TX and RX bytes, the number of bytes
queued or still being sent, and any framing errors.

At most 4,096 queued bytes (about 4.3 virtual seconds of back-to-back frames) are
allowed. A send that doesn't fit is rejected whole: nothing is queued, the text
stays in the field and a message explains why. If only the queue is full, the
message shows how much space is free; running or stepping frees space as frames
finish. A single text larger than 4,096 bytes can never be queued, and the
message says to send it in shorter parts.

## Scrollback, clearing and reset

The scrollback keeps the newest **1,000 rows**. Older rows are removed, and a
line above the scrollback shows how many. While the view is at the end it follows
new output, including after the limit is reached. Scrolling back with the wheel,
the scroll bar or the keyboard stops following, so the view stays still while
output arrives. **Latest ↓**, or scrolling back to the end, resumes following.
Tab into the scrollback to use the arrow keys, Page Up/Down, Home and End (End
resumes following). Clicking a row does not move keyboard focus.

**Clear** empties the scrollback and any send message. Output decoded before the
click is cleared too, even if it had not appeared yet. Clear does not clear the
board's decoded bytes, queued input, structured log, session totals or virtual
time.

**Reset** adds a notice row once its pulse completes. The row comes after
everything stamped at or before the pulse start, and before output decoded
during the pulse. A pulse that fails with a simulation error adds no row; the
toolbar shows the error. Queued RX input keeps sending during and after the
pulse. The RTL loses the frames it was
receiving or sending at that moment, so output right after a reset can include
unexpected bytes or framing errors. The terminal shows exactly what the board
decoded.

## Architecture

The data path is `BoardModel` → `BoardAdapter` → `UartConsoleModel` → QML.

`BoardAdapter::refresh()` copies newly decoded bytes, framing errors and their
stamps from BoardModel. It reads everything first and only then notifies views,
the same order the other board models use. Bytes and notices are ordered by
stamp, and Clear first consumes decoded traffic, so neither the refresh rate
nor wall-clock time can change the rows. The console model is
read-only for QML: sending (`sendUartText`) and clearing (`clearUart`) are
`BoardAdapter` methods, which reject calls from the wrong thread and calls made
while a notification is being delivered. After a completed reset pulse, the
controller calls a private adapter method to add the notice. QML sees no engine, binding or
signal handle, and stepping, decoding and timing never happen in QML.

The backend gained three read-only additions and no change in behavior:

- `BoardModel::uartTxByteCycles()` records each byte's log stamp next to
  `uartTxBytes()`, even when logging is off.
- `BoardModel::uartTxFramingErrorCycles()` records each framing error's log stamp.
- `sendUart()` returns the byte's scheduled start-bit cycle. That is its RX log
  stamp whenever edges are applied through `tick()`, which is the only way the
  Qt frontend advances time.

Byte values, bit timing, log lines and golden files are unchanged.

## Limitations

- The terminal is available only through the built-in `uart_echo` launcher;
  the counter and stopwatch bind no UART pins. Loading arbitrary designs is still
  deferred, and the UART rate is fixed at 9600 baud.
- The board's decoded-byte list and its stamps still grow without a limit, as the
  frozen simulator invariants document. Only the terminal's scrollback and input
  queue are limited.
- TX stamps are rounded forward to the 1,000-cycle observation grid.
- At the default window size the scrollback shows about four rows; drag the
  separator above the output panel to enlarge it (up to 400 px).
- There is no input history or VT100 emulation. Typing a newline in the input
  field is not possible; use the line-ending menu instead.

## Verification

```sh
ctest --test-dir build/qt -R '^qt_uart_|^qt_qml_uart_smoke$|^uart_|^peripheral_contract$' --output-on-failure
cmake --build build/qt --target virtualbasys_qt_uart_qmllint
./build/qt/src/qt/virtualbasys_qt_uart --smoke-test
VB_QT_SCREENSHOT_DIR="$PWD/build/qt/uart-captures" ./build/qt/src/qt/test_qt_uart_ui
```

- **`qt_uart_console`** tests the adapter and model without Verilator. A
  synthetic engine produces TX waveforms, and expected stamps are computed
  independently from the observation grid. Coverage:
  - row contents, hex and exact time text, identical at different refresh rates
  - all 256 byte values, splitting of long rows, CRLF and escape handling
  - RX start cycles, deferral during a stop bit, UTF-8 byte counts
  - the whole-send rule, oversized and unencodable text, and queue recovery
  - one notice per framing error, identical at different refresh rates
  - trimming at 1,000 rows, including a single oversized batch, and the count
    signal while the row count stays at the limit
  - Clear leaves board state untouched and removes unshown earlier output
  - reset notices ordered around a byte decoded during the pulse, and no notice
    after a failed pulse
  - calls from the wrong thread or during notification (including during
    Clear), and QML type exposure
- **`qt_uart_ui`** runs the real `uart_echo` RTL through actual keyboard, mouse,
  wheel and scroll-bar events. It checks:
  - exact echo and structured-log stamps, and the hex view
  - repeated sends, and Pause/Resume in the middle of output
  - Reset during traffic, with ordering against the pulse cycle
  - LF, CR and CR+LF bytes, following and "Latest", and Clear
  - following past the 1,000-row limit, the trimmed-rows line and keyboard
    scrolling
  - the queue-full message, the minimum window size and a real-timer run
  - TX-only and RX-only designs, and the unavailable states for a missing
    board and for a board with no design
  - no QML warnings, including binding loops, in any case
  The QCoreApplication-only run prints QtTest's known "invalid type, type id:
  4097" model-tester diagnostic, also documented in [qt_adapter.md](qt_adapter.md).
- **Backend tests.** `peripheral_contract` and `uart_echo` assert that the new
  stamps equal the log's own stamps. They also assert that the stamps don't
  depend on how ticks are split.
- **Mutation checks.** Seventeen deliberate faults were each injected
  temporarily; each made a test fail. They include every defect found by the independent
  reviewers:
  - Clear keeping earlier unshown output
  - following stopping at the row limit
  - a height-bound list header causing a binding loop
  - merged framing notices
  - a notice after a failed pulse
  - a missing reentry guard in Clear
  - a wrong CR ending
  - no count signal at the limit
  - misleading advice for oversized text
  - part of a text sent when it has an unpaired surrogate
  - a leading byte-order mark rejected

  The rest cover CRLF, TX/RX ordering, queue and row-limit off-by-ones, reset
  ordering and resuming at the end.

## Performance

```sh
cmake -S . -B build/qt -DVB_BUILD_QT_GUI=ON -DVB_BUILD_GUI=OFF -DVB_BUILD_BENCHMARKS=ON
cmake --build build/qt --target benchmark_qt_control_uart_echo -j 8
./build/qt/bench/benchmark_qt_control_uart_echo 20000000 3
```

The UART benchmark is the M4 controller benchmark with one addition: after the
shared 16-cycle reset, it queues 512 bytes of newline-terminated text through the
terminal. The echo therefore runs through the 2-million-cycle warmup and the whole
measurement. The rendered window opens on the UART tab, so new rows are added and
displayed while throughput is timed. At 22 million cycles the terminal showed
210 echoed bytes and 301 bytes still queued.

Measured on 2026-09-23 with macOS 26.5.2 arm64 and Qt 6.11.2. The build used the
default CMake build type, with the existing `-O2` for generated and backend code.
Each path ran three times for 20 million cycles, serially and in the foreground.
The table shows medians. The counter was measured again in the same session to
check whether adding UART work to each refresh changed M4's results.

The measured code has the final refresh and follow logic. Two later changes are
not on the measured path: a constant 2 px margin, and a UTF-8 check that runs
once when text is sent, before timing starts. Attempts that stopped on the
benchmark's own foreground check (because another window took focus) were
discarded, including later attempts to re-measure the final build while the
desktop was in use.

| Design | Direct board, Mcycles/s | Controller, Mcycles/s | Rendered turbo, Mcycles/s | Rendered 1× target, Mcycles/s | Turbo Qt presentations/s |
| --- | ---: | ---: | ---: | ---: | ---: |
| UART echo, terminal live | 18.431 | 15.257 | 14.852 | 14.838 | 96.07 |
| Counter, same session | 21.413 | 17.294 | 16.836 | 16.630 | 56.40 |

[Raw samples](qt_uart_performance.csv) include actual cycles, elapsed time and
presentation counts. Results:

- **Terminal cost.** Rendered turbo with the terminal live is 19% below the paired
  direct-board rate. The counter's gap is 21%, so the terminal adds no measurable
  cost beyond the existing 1 ms GUI yield and rendering.
- **Direct board rate.** The direct rate is within 1% of the P3 UART baseline
  median of 18.590 Mcycles/s.
- **Counter against M4.** All of the counter's rates are at or above M4's results
  (direct 21.188, controller 16.536, rendered turbo 15.824). The UART changes
  cost it nothing measurable.
- **Presentations.** Qt presentations are frames drawn, not simulated frames.
  While following, the terminal re-positions the view after each update. The
  view calls ListView's `positionViewAtEnd()` even when already at the end,
  because skipping it based on cached geometry stopped following at the row
  limit (caught by the tests). This likely explains the extra frames: UART 96/s
  against the counter's 56/s, with no measurable throughput cost. The rates also vary between sessions:
  an earlier session measured about 61/s and 49/s.

These are small local samples, not guarantees. Real-time speed (100 MHz) is still
not reached.
