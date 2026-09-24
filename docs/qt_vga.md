# Qt VGA monitor

M6 shows a design's VGA output in the Qt board. The existing monitor model in
`BoardModel` samples the sync and colour pins every cycle, measures the pixel
clock, rebuilds and checks each frame. Qt displays the latest completed frame,
exactly. No simulator code changed.

```sh
./build/qt/src/qt/virtualbasys_qt_vga
```

`virtualbasys_qt_vga` runs the built-in `examples/vga_pattern.v`: eight colour
bars at 640 × 480 with a 25 MHz pixel clock (4 master cycles per pixel), a white
top and sides, and a blue bottom row. Like the UART launcher, it applies one
16-cycle Reset before the window opens, so it starts at cycle 16. The design's
sync outputs start low (asserted) until btnC reset (R6), which would give the
monitor a false first edge. The legacy demo applied the same reset.

## What you see

Designs that drive all 14 VGA pins get a **VGA monitor** card at the top of the
board. Other designs, and the unloaded preview, have no card.

- **Screen.** The latest completed frame, drawn at a whole-number scale.
  Normally this is 1×, which fits the default window even when macOS limits the
  window height on a laptop screen. A larger window can show it at 2×.
- **No frame yet.** Before the first complete frame the screen says so. The
  monitor needs two vertical syncs to delimit a frame; for `vga_pattern`, frame
  0 completes at cycle 3,248,019.
- **Signal problem.** If the monitor finds a timing problem in the latest frame,
  such as an active-high sync, a wrong line period or an incomplete frame, a
  "Signal problem" line appears over the frame. The frame stays visible. A reset
  in the middle of a frame produces this, and the next clean frame clears it, as
  on a real monitor.
- **Text under the screen:** the frame number, the cycle and virtual time at
  which it completed, and the measured cycles per pixel. While running, it also
  shows how many simulated frames per second the measured speed gives, against
  59.5 at real time (100 MHz ÷ 1,680,000 cycles per frame).
- **Footer.** It shows the latest frame number and its completion cycle, so they
  stay visible without scrolling.

Frames are exact whatever the wall-clock speed. The Qt window shows the newest
completed frame each time it updates; if several frames complete between two
updates, only the latest is drawn. Every frame is still produced and validated
by the monitor model. Virtual time decides when a frame completes; wall time
only decides how often the window updates.

## Pixel exactness

A displayed image is pixel-exact only if every screen pixel shows exactly one
framebuffer pixel. Three things ensure this:

1. **Whole-number scale and nearest filtering.** The screen is exactly 640 × 480
   times a whole number, and the texture uses nearest filtering, so pixels are
   never blended. Together with snapping (below), each framebuffer pixel covers
   an exact square of `scale × devicePixelRatio` screen pixels.
2. **Snapping to device pixels.** Layouts can place the display at fractional
   positions, for example a label's height or a 0.25-point offset on a 2×
   display. With the display half a device pixel off, samples land on texel
   boundaries, and on Metal about half the pixels showed the wrong framebuffer
   pixel. `VgaDisplay` therefore shifts its image to the nearest whole device
   pixel whenever it draws. It also re-checks its position and the screen's
   pixel ratio every frame, so scrolling, layout changes or moving the window to
   a screen with a different ratio can't leave it misaligned. The board also
   scrolls only by whole pixels.
3. **Exact data.** The adapter copies the board's RGB888 framebuffer unchanged:
   top row first, each 4-bit channel multiplied by 17.

## Architecture and the texture decision

The data path is `BoardModel` → `BoardAdapter` → `VgaFrameModel` → `VgaDisplay`
(QML).

- `BoardAdapter::refresh()` copies the framebuffer only when the completed-frame
  count changes. It never copies on ordinary refreshes, and all board reads
  happen before any notification, as for the other models.
- `VgaFrameModel` is read-only in QML. It exposes the frame count, completion
  stamp, measured cycles per pixel and monitor status. C++ code gets the image
  and a serial number that changes whenever a new frame arrives.
- `VgaDisplay` is a scene-graph item. It creates one texture per new frame and
  otherwise redraws the existing one.

The roadmap left one choice open: a scene-graph texture item, or a QImage drawn
by a painted item. `benchmark_qt_vga_display` settled it by measurement. It
compares the production `VgaDisplay` with a benchmark-only `QQuickPaintedItem`
that draws the same QImage with nearest scaling, plus a run with no window. All
three run the real `vga_pattern` simulation under the real controller. For
each displayed frame, it records the render thread's synchronize-and-render
time, separately for frames that uploaded a new VGA image.

| Display | Mcycles/s | Simulated frames/s | Render-thread time, frame with a new image | Render-thread time, other frames |
| --- | ---: | ---: | ---: | ---: |
| No window | 12.344 | 7.35 | — | — |
| **Scene-graph texture (chosen)** | 12.172 | 7.24 | **0.40 ms** | 0.08 ms |
| Painted QImage | 12.244 | 7.29 | 1.56 ms | 0.09 ms |

Medians of three runs of 16.8 million cycles (10 frames each);
[raw samples](qt_vga_display_performance.csv). Displaying a new frame costs
about 0.3% (texture) or 1.1% (painted) of the ~138 ms of simulation per frame,
so throughput differences are within noise. The texture path costs about a quarter of the
painted path per new frame, and the GPU scales it without CPU painting, so it is
used. The window redraws only when a frame or text changes.

## Performance of the full window

```sh
cmake -S . -B build/qt -DVB_BUILD_QT_GUI=ON -DVB_BUILD_GUI=OFF -DVB_BUILD_BENCHMARKS=ON
cmake --build build/qt --target benchmark_qt_control_vga_pattern benchmark_qt_vga_display -j 8
./build/qt/bench/benchmark_qt_control_vga_pattern 20000000 3
./build/qt/bench/benchmark_qt_vga_display 16800000 3
```

The controller benchmark from [qt_control.md](qt_control.md) now includes
`vga_pattern`. It also has a `vga_frames` column (simulated frames completed)
next to `frames` (frames the Qt window drew). Measured on 2026-09-24 with macOS
26.5.2 arm64 and Qt 6.11.2, on a native 2× display. It ran serially in the
foreground and has three samples of 20 million cycles per path; the table shows
medians.

| Path | Mcycles/s | Simulated VGA frames/s | Qt frames drawn/s |
| --- | ---: | ---: | ---: |
| Direct board | 14.347 | 8.61 | — |
| Controller | 12.001 | 7.20 | — |
| Full window, turbo | 11.825 | 7.09 | 58.5 |
| Full window, 1× target | 11.805 | 7.08 | 59.2 |

[Raw samples](qt_vga_performance.csv).

- **Where the cost goes.** The full window is 17.6% below the direct board.
  Almost all of that (16.4 points) is the controller's fixed batches and 1 ms
  pause between batches, which keeps the window responsive. Drawing the window
  and the monitor costs the remaining 1.2 points (1.5% of the controller's
  rate).
- **Direct board.** 3.8% below the pre-migration VGA baseline of 14.910 in this
  session. The VGA simulation path is unchanged.
- **Real time.** Real-time VGA (59.5 frames/s) would need about 100 Mcycles/s,
  and is not reached. The window reports the true speed.

## Verification

```sh
ctest --test-dir build/qt -R '^qt_vga_|^qt_qml_vga_smoke$|^vga_' --output-on-failure
cmake --build build/qt --target virtualbasys_qt_vga_qmllint
./build/qt/src/qt/virtualbasys_qt_vga --smoke-test
./build/qt/src/qt/test_qt_vga_ui   # native: required for the snapping check
```

- **`qt_vga_model`** tests the adapter with a synthetic design that generates
  exact 800 × 525 timing at one cycle per pixel. Every frame is different and
  exercises every colour bit. Expected images and completion stamps come from
  that timing arithmetic, independent of the monitor model. It covers:
  - no VGA, and an incomplete set of VGA pins;
  - the exact cycle at which the first frame completes;
  - a frame is copied only when it is new, and the copy is not affected by later
    frames;
  - several frames completing between refreshes;
  - an active-high sync is reported and later clears;
  - every model is staged before any is notified (a frame listener sees the new
    button state, and a button listener sees the new frame);
  - refresh never advances time, cannot be re-entered, and is rejected from
    other threads;
  - what QML can access.
- **`qt_vga_ui`** runs the real `vga_pattern` RTL through the real window and
  controls. It grabs the window and compares every screen pixel of the monitor
  with the RTL's colour-bar pattern, plus fixed checks for yellow, the white top
  and the blue bottom. Scenarios:
  - Step until frame 0, then Run until later frames. Each frame's completion
    cycle must match a formula derived independently from the RTL timing (frame
    0 at 3,248,019 after the 16-cycle reset).
  - The measured frame-rate text, using an injected clock.
  - Reset mid-frame: the monitor's diagnosis is shown, then clears.
  - The minimum window size, a laptop-clamped window height, scrolling and the
    footer layout.
  - The synthetic design in a separate 1320 × 1000 window, with the display
    offset by 0, 0.25, 0.5 and 0.75 points at 1× and 2×. The same window
    covers swapping to another model, the display clearing when its model is
    destroyed, and no repeated redraws at zero size.
  - 2× in the real shell when the side panes are hidden and the window is wide.
  - The footer readout and its warning colour.
  - Natively only: a display created from C++ re-snaps after its parent moves
    by a fraction, and one moved between 2× and 1× screens stays exact. Both
    are skipped offscreen; the screen move also needs both kinds of screen.
  - The unavailable cases.

  Any QML warning fails the test.
- **Native and offscreen runs.** The UI test passes both offscreen and natively on
  Metal at 2×. The offscreen software renderer rounds image positions itself,
  so snapping is only really tested natively; the CTest run is offscreen, so run
  the native test after changing `VgaDisplay`. Natively, removing the snap makes
  590,612 of 1,228,800 screen pixels wrong at a 0.25-point offset, and skipping
  the re-snap after a move to a 1× screen makes 259,990 of 307,200 wrong.
- **Mutation checks.** Eighteen deliberate faults were each introduced
  temporarily; each made a test fail. Four are caught only natively:
  - no snapping; no re-check after moving; no re-snap after a pixel-ratio
    change; no window hook for a display created from C++ (native only);
  - linear filtering; uploading only the first frame; copying on every
    refresh; BGR channel order; a missing change signal;
  - notifying the frame before the other models are staged;
  - a zero-size redraw loop; a model swap that keeps the old image; no clear
    when the model is destroyed;
  - a wrong scale (rounded up, or fixed at 1×); an off-by-one frame number; the
    waiting message never hiding; the footer warning colour removed.

  One further mutation, a non-integer scale, turned out to have no effect: QML
  stores the scale as an integer, which truncates it. The suite does not
  count scene-graph textures. A reviewer's probe, which watched each texture's
  `destroyed()` signal, confirmed one texture per new frame, each previous one
  freed and at most one alive at a time.

## Limitations

- VGA runs only through the built-in `vga_pattern` launcher; loading arbitrary
  designs is still deferred.
- Only the latest frame is drawn when several complete between window updates.
  All frames are still produced and checked.
- At the default window size the text under the screen is below the visible
  area; scroll the board to see it. The footer shows the frame number and cycle.
- At the measured ~7 simulated frames/s, animated designs move about eight
  times slower than on real hardware. That is the simulation speed; no frames
  are lost or altered.
- Only whole-number scales are used, so the monitor never shrinks below 1×. In
  a small window, scroll the board.
