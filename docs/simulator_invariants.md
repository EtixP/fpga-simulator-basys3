# Simulator invariants

Frozen after P3 correctness fixes, full normal/sanitizer suites and two independent
PASS reviews on 2026-09-18. Qt migration must preserve this contract.

## Engine and virtual time
- One cycle advances one rising edge of the engine-owned 100 MHz master clock:
  10 ns virtual time. Wall-clock time, GUI frame rate and pacing cannot affect it.
- Construction completes initial/declaration assignments and memory initialization,
  zeroes otherwise uninitialized state and inputs (two-state model), settles logic,
  leaves master clock low and now()==0. No artificial rising/falling edge occurs.
- Each cycle evaluates low then high. First cycle starts already low; subsequent
  cycles include a falling edge before the rising edge. Returned state is settled,
  post-rising-edge, clock high. now() counts completed rising edges.
- step(0) advances/evaluates nothing. step(N) equals any partition totaling N when
  input changes occur at the same absolute cycles. Derived clocks produced by
  supported RTL evaluation are honored; independent asynchronous CDC is not modeled.
- poke writes only top-level inputs, masks to width, rejects the master clock,
  outputs/internal/invalid handles. It advances no time. Pending inputs settle at
  peek or the next step's low evaluation, before its rising edge.
- peek settles pending changes without toggling the master clock, advancing time
  or dumping traces. This can trigger RTL asynchronous reset/input-sensitive logic;
  synchronous inputs affect the next rising edge N+1 after a poke at now()==N.
- Signal handles belong to one engine; pair engines by names, never numeric IDs.
  Plain lookup resolves supported top-level ports; hierarchical debugging is
  best-effort/read-only. Unsupported wide/unpacked/noninteger/inout signals are
  omitted. Supported integer values are packed unsigned 1..64-bit words.
- stepCapture is exactly repeated step(1) + packed peeks: LSB-first in watch-list
  order, each width masked, total <=64. Empty watch sets write zeros. Zero cycles
  leave the output untouched but still validate IDs/width. Rejected captures do
  not advance time. The caller supplies sufficient buffer space.
- VCD uses 1 ns precision; cycle index N dumps low at 10*N and high at 10*N+5.
  Trace enable is lazy; disable flushes/pauses; resume appends without truncation.
  No peek/poke-only dump. Trace filename is fixed once successfully opened.
- Each engine owns its context/model/tracer; multiple engines may interleave and
  be destroyed in any order. Engine operations are single-threaded; cross-thread
  use requires an explicit future ownership design, not concurrent access.

## Board observation and inputs
- BoardModel owns an immutable package-pin binding; its engine must outlive it.
  Frontends access simulation through BoardModel. QML will only see a Qt adapter.
- tick advances exactly its cycle argument; slow outputs are observed only on the
  absolute 1000-cycle grid. Trailing partial chunks never add observations.
- tick(0) advances no time but applies UART RX edges due now (including queued
  start bits) and records their input events. This differs intentionally from step(0).
- Equivalent input schedules and total cycles produce identical board state,
  decoded bytes, completed frames and logs regardless of tick partitioning.
- Switches latch; buttons persist until explicitly cleared. Debounce belongs to RTL.
  Unbound resources ignore writes and read inactive/dark. Invalid indices are safe.
- LEDs read the current settled pins; seven-segment reads the sampled persistent
  view. Thus a live LED may change before its next grid-observed log event.
- Seven-segment anodes/cathodes/DP are active low. Each sampled active digit latches
  its lit segments/DP through lastLit+2,000,000 cycles inclusive, then becomes dark.
  All simultaneously active anodes capture. Missing pins are inactive. Active windows
  >=2000 cycles are a supported sufficient capture guarantee; shorter windows may
  alias. Decode is character-level; unfamiliar patterns are '?'.

## UART
- Board UART is 8N1, LSB first, idle high, 10417 master cycles/bit (~9600 baud).
  RX is host→design, TX design→host. Byte values are unsigned, including NUL/0xFF.
- RX edges are exact-cycle and split tick runs. Queued frames start back-to-back;
  the prior stop bit owns its full bit duration. RX byte logs stamp the start bit.
- TX is grid-observed: first low observation anchors a candidate start; validate
  its midpoint, sample data at 1.5+n bit times and stop at 9.5, rounded forward to
  observation points. Candidates high at the validation midpoint are rejected;
  low stop yields framing error.
  Observation spacing must stay within the supported >=4 chunks/bit envelope.
- A valid sustained echo stream must not drop bytes merely because transmitter
  scheduling accumulates one extra cycle per frame. Reset discards RTL in-flight state.
- Decoded byte collection and optional logs currently grow without a bounded cap.

## VGA
- Monitor supports 640x480 active pixels, 800 pixel periods/line, 525 lines/frame,
  active-low Hsync/Vsync. Diagnose sync duty polarity, nonintegral/changed line
  periods, frame duration, scanline counts and incomplete pixels. This is not
  exhaustive pulse-width/porch validation; publish the latest frame status.
- Measure integer master cycles/pixel from Hsync period/800. Reconstruct visible
  centers at Hsync fall +144 pixel periods +column*cpp +floor(cpp/2), with 35 lines
  from Vsync fall to visible row zero. Coincident sync falls do not add a row.
- Successive Vsync falls delimit completed periods. Latest completed framebuffer is
  top-origin RGB888, 640*480*3 bytes; each four-bit channel expands by multiplication
  by 17. Status belongs to that period and recovers after a subsequent clean period.
- Capture-slot timestamps are zero-based: sample i in capture starting at now()==N
  carries post-edge state N+i+1 but monitor stamp N+i. Preserve this historical
  convention: shipped reset fixture frame index 1 completes at stamp 4,928,013.
- Requires all 14 VGA pins and <=64 total bits across distinct watched whole ports.
  Otherwise no VGA is exposed. Before first completion, no frame has been validated.

## Structured log and constraints
- v2 header and current golden files are frozen. Collection is opt-in; only
  transitions/byte events are emitted. Clear restores header without resetting
  observations; disable/re-enable does not create a snapshot or replay old events.
- At grid crossings: SSEG digits ascending (character then DP), LEDs ascending,
  UART TX byte/error, then UART RX edges. Caller input transitions follow in call
  order at exact now(); per-bit RX transitions and VGA frames are not logged.
- Package pins identify board resources independently of port names. HDL declared
  indices map to packed offsets, including ascending/nonzero/signed ranges; only
  top-level ports bind. Default Basys mapping matches all 105 pristine master pins.
- XDC is a literal constraints subset, never a Tcl interpreter. Unsupported commands,
  expressions, wildcard/range/multi-port targets warn and skip. Duplicate properties
  use last value; duplicate package pins warn and use last binding.
- create_clock is stored metadata only; it cannot change the 100 MHz simulation clock.
  Literal supported documents survive canonical parse/write/parse serialization.

## Scripted frontend startup
- Positive-length demo runs apply automatic reset from cycle 0 through 16 while
  servicing scripts at their exact absolute cycles; logging includes startup.
  Explicit BTNC scripts take ownership of reset. Same-cycle switches/buttons
  precede UART sends; order within each type is stable.
- A zero-frame launch performs no reset, scripts or advancement. Normal run horizon
  is 16 + frames*cyclesPerFrame. GUI measurements describe wall-clock throughput,
  never alter simulation results. Interactive event cycles depend on when delivered.
