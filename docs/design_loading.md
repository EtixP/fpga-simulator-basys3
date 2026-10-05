# Running your own designs

`virtualbasys run` runs any synthesizable Verilog or SystemVerilog design on the
virtual Basys 3.
The design is verilated and compiled the first time you run it, then cached.

```sh
./build/qt/bin/virtualbasys run top.v
./build/qt/bin/virtualbasys run rtl/top.v rtl/uart.v -I rtl/include --xdc pins.xdc
./build/qt/bin/virtualbasys run multi.v --top board_top --frames 300 --log run.log
```

Source files come first, straight after `run`. Then:

| Option | Meaning |
| --- | --- |
| `--top NAME` | The top module. Needed only when the sources have more than one. |
| `-I DIR` | An `` `include `` search directory. Repeatable; `-IDIR` also works. |
| `--xdc FILE` | Your constraints. Without it, the Basys 3 master constraints apply. |

Every other launcher option works as for the built-in examples: `--frames`,
`--at`, `--switches`, `--send`, `--log`, `--screenshot`, `--realtime` and more;
see [scripted runs](qt_scripted_runs.md). `virtualbasys --help` lists them all.
`--preview` shows the board without a design and cannot be combined with `run`.

## What you see

The first run of some sources prints

```
virtualbasys: compiling 'top' with Verilator (first run of these sources; later runs reuse it)
virtualbasys: compiled in 9.0 s
```

and then opens the window. Later runs of the same sources open in about half a
second.

Verilator's own messages are printed as it writes them, each line prefixed
`verilator:` (requirement R5):

- **Warnings** do not stop the design. Code that Vivado accepts, such as a width
  mismatch, runs with its warnings shown. So do inferred latches (`LATCH`) and
  combinational loops (`UNOPTFLAT`), which are usually bugs. `-Wall` is not
  used for your designs.
- **Errors** stop before any window opens, with exit code 1:
  `verilator: %Error: top.v:3:1: syntax error, unexpected endmodule`.

Mistakes that need no compile to find are reported before anything is
compiled, with exit code 1:

- option mistakes, such as `--frames x`, `--top` given twice or empty, or source
  files after an option;
- a constraints file given as a source, or an `--xdc` file that cannot be read;
- a missing source file, or several top modules without `--top`;
- an `` `include `` by an absolute path that the build cannot use (see Paths);
- a design without an input for the 100 MHz clock (see below).

Ctrl-C, SIGTERM or closing the terminal during a compile stops it at once:
`virtualbasys: interrupted`, exit code 130, and nothing is cached.

## The Verilog you can use

- **Verilog and SystemVerilog.** As Vivado reads them, `.sv` sources are
  SystemVerilog (`logic`, `always_ff`, `always_comb`, `enum`, `unique case`,
  packages and so on) and every other source is Verilog-2005 (IEEE 1364-2005),
  where SystemVerilog constructs are Verilator syntax errors (R5). The
  extension is matched as written: `.SV` is read as Verilog-2005. An
  `` `include ``d file is read in the language of the file that includes it.
  Constructs beyond the synthesizable subset, such as classes, are whatever
  Verilator makes of them.
- **Other deferred constructs** (R5) are not refused by VirtualBasys itself.
  Latches and loops get Verilator's warnings, as above. Xilinx primitives such
  as an MMCM are modules Verilator cannot find, which is an error. Anything
  else runs as Verilator simulates it, with one master clock (R1).
- **Includes.** `` `include `` files are searched in the `-I` directories, then
  in the directory you run `virtualbasys` from, not in the including file's
  directory: this is Verilator's rule.
- **Memory files.** `$readmemh` and `$readmemb` paths are relative to the
  directory you run `virtualbasys` from, as for any simulation binary.
- **`$finish`, `$stop`, `$fatal`.** They behave as in the built-in examples,
  which share the engine. `$finish` prints Verilator's message and the board
  keeps running; a design that reaches it again prints it again. `$stop` and
  `$fatal` end the app, as they end a Verilator simulation. Keep them in
  testbenches (R5).
- **Paths.** Your files and folders may have spaces, semicolons and non-ASCII
  characters in their names, also where `` `include `` text spells them. CMake
  cannot build from such paths, so such a design is built from copies of the
  files Verilator read, with its symbolic links recreated. The exception is an
  `` `include `` by absolute path: the build reads that file where it is, so
  neither that path nor where it leads may have them. Such a design is refused
  before compiling; include the file through an `-I` directory instead.

## Constraints, clock and reset

- **Constraints.** Without `--xdc`, the design uses the Basys 3 master
  constraints with every pin enabled, so its ports need the master file's
  names: `clk`, `sw`, `led`, `btnC`, `btnU`, `seg`, `an`, `dp`, `RsRx`, `RsTx`,
  `vgaRed` and so on. Board resources the design does not use are simply left
  out. A port the master file does not name gets a `bind warning:` line and a
  hint listing the names. With `--xdc`, binding follows your file, and
  mistakes in it are reported as `bind warning:` lines.
- **Clock.** The 100 MHz clock is whichever port the constraints bind to pin W5,
  like every other board resource; any port name works. A design without that
  input is refused before it is compiled.
- **Reset.** A design that binds btnC gets one 16-cycle Reset pulse before the
  window opens, as the built-in UART and VGA examples do (R6: examples reset on
  btnC). Scripted runs always start with that reset.

## How it works

1. **Check.** Every run first runs Verilator's lint pass, `verilator
   --lint-only`, for all its messages, then its front end, `verilator
   --json-only`, for the top modules, the top's input ports and every file it
   read, `` `include `` files included. Together they take about 0.13 s.
2. **Cache.** The module for a design is keyed by:
   - the contents of every file read;
   - the top module, and the source and include directory lists;
   - the Verilator version and installation, and the compiler;
   - the engine's own sources and the build template.

   Entries live in `~/Library/Caches/VirtualBasys/designs` (or
   `$VB_DESIGN_CACHE`). Delete the folder to reclaim space; nothing else refers
   to it. A read-only cache still serves the modules it has.
3. **Build.** On a cache miss, a small CMake project in `src/design/module`
   verilates the design with the examples' flags (`--public-flat-rw
   --trace-vcd --timescale 1ns/1ns`, `-O2`) plus `-Wno-fatal`,
   `--default-language 1364-2005` and `+1800-2023ext+sv`. It compiles the
   design into one loadable
   module, `design.so`, together with the engine code the examples use
   (`VerilatorEngine`).
   - **Isolation.** The build runs in a private directory under `$TMPDIR`, with
     your working directory as the last include directory, as in the check.
     From your environment it gets only `PATH`, `HOME`, `USER`, `LOGNAME`,
     `SHELL`, `TMPDIR`, the locale and `VB_` variables, so variables such as
     `CXXFLAGS`, `MACOSX_DEPLOYMENT_TARGET` or `VERILATOR_ROOT` cannot change it.
   - **Storing the module.** A module is stored only if the build read exactly
     the files the check read, the same files by identity and size, and none of
     them, no symbolic link on the way to them and none of the engine's own
     sources changed during the build, not even by an edit that was undone;
     otherwise run again. It is published with
     one rename, so an interrupted or concurrent build never leaves a
     half-built module. A cache that cannot take a new module, or an entry
     that cannot be read, is reported before any compile. Work directories of
     builds that were killed are removed by the next run.
4. **Load.** The `virtualbasys` app loads the module and runs it through the
   same board, adapter, controller and window as the built-in examples. The app
   itself contains no Verilator code, so the module brings the process's one
   Verilator runtime. The module is never unloaded.

A module and the app must agree on the engine interface. Both compile the same
interface hash, from `engine/SimEngine.h` and `design/ModuleAbi.h`, and must use
the same C++ library ABI version. A module that fails that check is refused.
A cached module that cannot be loaded, whether damaged or built for another
VirtualBasys, is built again once. After a compiler update, existing modules
keep working.

The built-in example launchers (`virtualbasys_qt`, `_stopwatch`, `_uart`,
`_vga`) remain. They link their design at build time and share the same
launcher code (`src/qt/Launcher.cpp`).

## Performance

Measured on 2026-09-28 (macOS 26.5.2, Apple Silicon, Verilator 5.050, Qt
6.11.2), offscreen:

| Example | First run: compile | Later runs: start (median of 5) |
| --- | ---: | ---: |
| counter | 9.0 s | 0.40 s |
| stopwatch | 9.2 s | 0.44 s |
| uart_echo | 8.5 s | 0.44 s |
| vga_pattern | 8.5 s | 0.41 s |

Of a later run's start, Verilator's two passes take about 0.13 s (0.065 s
each for the stopwatch). Most of a first compile is the Verilator runtime,
which every module includes. Simulation speed is unchanged: the same
100-million-cycle stopwatch script took 6.98 s through `virtualbasys run` and
7.27 s through the prebuilt launcher (medians of 3, including start-up and the
window), 14.3 and 13.7 Mcycles/s.

## Verification

- **`design_loader`** (headless, no Qt, no design linked) builds real modules:
  - the counter's arithmetic through a loaded engine, with a cache hit and a
    rebuild after an edit;
  - warnings shown while the design runs, including `LATCH` and `UNOPTFLAT`;
    a syntax error with Verilator's exact message; SystemVerilog refused in
    `.v` files;
  - a `.sv` design built and run: an enum state machine from an included
    `.svh`, `always_ff`, `unique case`;
  - several tops with and without `--top`, and an unknown top;
  - include directories, including a rebuild after editing only an included file;
  - a design in a directory with a space, a semicolon and Hangul, with
    includes found through the working directory, through `../` from an
    include directory, by Hangul and spaced names, by absolute path, and
    through file and directory links (`..` after a link goes to the target's
    parent), and a cache path with a space;
  - `$readmemh` relative to the working directory;
  - a module for another ABI refused, and a file that is no module;
  - Verilator's octal escapes and a tree hundreds of levels deep;
  - with a fake CMake, the cache's rules:
    - the top and the engine sources in the key, and the caller's check
      before any build;
    - nothing stored after an edit during the build: also an undone one, even
      with its modification time put back, and also to a copied design's
      original, to a link on the way or to an engine source;
    - nothing stored when the build read a file the check did not (also one
      named like the Verilator program), or another file under a checked name;
    - an unusable absolute include refused before any build;
    - the failing step's messages only, tools that cannot run, only allowed
      variables passed on, and a Verilator installation that forwards to
      another;
    - damaged entries rebuilt, a read-only cache and an unreadable entry
      reported before any build, abandoned work directories removed, and
      cancellation.
- **`qt_design_run`** runs `virtualbasys run` as a user does, in separate
  processes:
  - all four examples write byte-identical logs to the prebuilt launchers for
    the same scripts;
  - cache reuse, default constraints with no `bind note:` lines, and a clock port
    named `sysclk`;
  - multiple files, `-I` and `--top`;
  - warnings, errors and every mistake above, with their messages and exit
    codes, before any compile;
  - `--help` anywhere but as another option's value, and `--preview`;
  - a build failure, a foreign cached module rebuilt, the unbound-port hint;
  - Ctrl-C and SIGHUP while compiling, SIGTERM while configuring: exit
    within 2 seconds, Verilator's warnings still shown, nothing stored, no
    compiler left running.
- **`qt_launcher`** drives the shared launcher body in process with a recording
  engine:
  - `virtualbasys run`'s design: the master constraints and the startup reset
    only when BTNC is bound; the Never and Always resets of the examples;
  - the clock port from the constraints, engine failures, and the window's
    title, file list and preview text;
  - the Project pane's paths: relative in the working directory, `~/` in the
    home folder.
- **Mutation checks.** 69 deliberate faults, each caught by the check meant
  for it, on a machine that stayed awake:
  - every review finding: a module stored after an edit during its build
    (also an undone one, through a link, or to an engine source), awkward
    paths and include names, links, deep trees, includes through the working
    directory, late-stage warnings, `run --preview`, and a repeated or empty
    `--top`;
  - each part of the cache key and of the file stamps, the build-input
    check, the ABI check, the Verilog-2005 and SystemVerilog rules, the clock
    and reset rules, and the Project pane's paths;
  - the early checks and their messages, `--help`, the build log, the hint;
  - cancellation (also by SIGHUP, with warnings kept), the build's own
    process group, damaged entries and rebuilds, and the build's environment.

The sanitizer build (`qt-asan`) instruments the app and the launchers. The
design modules it compiles at run time are built without sanitizers.

## Requirements and limits

- Verilator, CMake and the compiler that built VirtualBasys must be installed:
  a design is compiled on your machine. The app finds them where they were at
  build time; `VB_VERILATOR_ROOT` (a Verilator installation, for both the
  check and the build) and `VB_CMAKE` override them.
- `virtualbasys` runs from its build tree: it compiles modules from this
  repository's sources. It is not packaged for installation.
- A design opens from the command line only; opening, reloading and a build
  output panel in the window are planned for M10.
- A loaded design's scripted-run frame is 100,000 cycles, including for VGA
  designs, whose built-in launcher uses 1,700,000.
