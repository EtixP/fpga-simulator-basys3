# Qt frontend development

M0 provides an optional Qt Quick window with an empty state. Simulator integration
starts with the BoardModel adapter in M1. The existing ImGui demos remain available.

## Dependencies and build

The optional frontend requires Qt 6.5 or newer with Core, Gui, Quick, Qml and
QuickControls2. Tested on macOS arm64 with Qt 6.11.2; see [versions.md](versions.md).
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

Qt discovery and autogen settings stay in the frontend subdirectory. The M0
executable links only Qt, with no simulator or BoardModel objects. Later QML
integration must go through a thin BoardModel adapter.

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

To check dependency isolation on a machine with Qt installed:

```sh
cmake -S . -B build/headless -DVB_BUILD_GUI=OFF -DVB_BUILD_QT_GUI=OFF \
  -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=TRUE -DCMAKE_DISABLE_FIND_PACKAGE_SDL2=TRUE
cmake --build build/headless -j 8
ctest --test-dir build/headless --output-on-failure
```

CMake may report the two disable variables as unused: neither package lookup is
reached when its frontend is disabled.
