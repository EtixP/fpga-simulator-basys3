# Qt frontend development

The optional Qt Quick frontend provides the IDE shell and M3 virtual board: project
navigation, switches, LEDs, buttons, seven-segment display, inspector and tabbed
output panel. The launcher shows a disabled board preview until design loading and
simulation control are integrated. Connected widget tests use the M1 adapter and
real RTL examples; the existing ImGui demos remain available for interactive runs.

## Dependencies and build

The optional frontend requires Qt 6.5 or newer with Core, Gui, Quick, Qml and
QuickControls2, plus Test for adapter and shell interaction tests. Tested on macOS
arm64 with Qt 6.11.2; see [versions.md](versions.md).
For Homebrew, install the base and declarative modules:

```sh
brew install qtbase qtdeclarative
cmake -S . -B build/qt -DVB_BUILD_QT_GUI=ON
cmake --build build/qt -j 8
./build/qt/src/qt/virtualbasys_qt
```

For a Qt installation outside CMake's search paths, pass
`-DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/macos` (the prefix containing `lib/cmake/Qt6`).
The usual Verilator/build dependencies still apply. This is a development
executable using the installed Qt runtime; standalone app packaging is deferred.
The QML module is embedded, so launch does not depend on the working directory.

`VB_BUILD_QT_GUI` defaults to `OFF`. It is independent of `VB_BUILD_GUI`, which
keeps its default `ON` and builds the legacy frontend on macOS only:

| Qt flag | Legacy flag | Result |
|---|---|---|
| OFF | ON | Existing demos; no Qt dependency |
| ON | ON | Both frontends |
| ON | OFF | Qt frontend; no SDL2/ImGui dependency |
| OFF | OFF | Headless libraries/tests; neither GUI dependency |

Qt discovery and autogen settings stay in the frontend subdirectory. The launcher
consumes the `VirtualBasys.Board` adapter module, which reads BoardModel and keeps
the simulator outside QML. The launcher has no loaded design yet. Ownership, API
and testing details are in [qt_adapter.md](qt_adapter.md).

## Using the shell

Drag the separators to resize the project, inspector and output panes. The toolbar
buttons toggle their visibility; **Restore layout** restores their default sizes,
shows all panes and returns to Board / Terminal. Layout changes are session-only.
Board and Overview select the central workspace; Terminal, UART, Logs and Waveforms
select the bottom panel. These panels explain their current empty state. Design
loading, command execution and simulation controls are not yet available.
Board behavior, input ownership and connected-example tests are described in
[qt_board.md](qt_board.md).

Keyboard shortcuts are Command+Shift+1 / 2 / 3 on macOS (Control+Shift elsewhere)
for Project / Inspector / Output, and Command+Shift+0 to restore the layout. With
full keyboard navigation enabled in the OS, Tab moves through controls; Space
activates a focused button, and arrow keys navigate the output tabs. Hiding a pane
moves focus to its toolbar button. Qt's Basic controls style and a shared dark
palette keep rendering consistent between tests and the app.

## Verification

```sh
ctest --test-dir build/qt --output-on-failure
cmake --build build/qt --target virtualbasys_qt_qmllint
./build/qt/src/qt/virtualbasys_qt --smoke-test
```

The optional `qt_qml_smoke` CTest runs with Qt's offscreen platform, software
renderer and Basic controls style. It requires a loaded window and a rendered
frame, fails on loading/rendering errors, and has a 10-second internal deadline
and a 20-second CTest limit. Running `--smoke-test` directly also exercises the
native platform/graphics path and exits after its first frame.

The `qt_shell` test uses real mouse and keyboard events against the source QML. It
checks navigation, tabs, pane visibility and focus, splitter dragging, layout
restoration, constrained geometry and disconnected/missing-adapter states. QML
warnings fail the test. The embedded module is independently covered by
`qt_qml_smoke`. To capture the native shell at its default and minimum sizes, with
hidden panes and with a missing adapter:

```sh
VB_QT_SCREENSHOT_DIR="$PWD/build/qt/shell-captures" ./build/qt/src/qt/test_qt_shell
```

Add `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software` for display-free captures.
Screenshots are review artifacts; tests check geometry and behavior rather than
platform-dependent pixel goldens. Layout/navigation advance no simulation time;
leaving the board or changing focus may release a held momentary input through the
adapter. Bound board controls use its validated input API. No simulation execution
loop exists yet, so the P3/M1 performance baselines remain
the reference. Rendered simulation throughput will be measured with M4's controller.

To check dependency isolation on a machine with Qt installed:

```sh
cmake -S . -B build/headless -DVB_BUILD_GUI=OFF -DVB_BUILD_QT_GUI=OFF \
  -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=TRUE -DCMAKE_DISABLE_FIND_PACKAGE_SDL2=TRUE
cmake --build build/headless -j 8
ctest --test-dir build/headless --output-on-failure
```

CMake may report the two disable variables as unused: neither package lookup is
reached when its frontend is disabled.
