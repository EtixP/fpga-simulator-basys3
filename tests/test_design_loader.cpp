// Design loading without Qt and without any prebuilt design library: real
// designs are verilated and compiled into modules at test time, cached and
// loaded through the SimEngine interface. Oracles: the RTL's own arithmetic,
// Verilator's diagnostics, and the cache's file-level behaviour. Cache and
// failure behaviour that needs no compiled module uses a fake CMake
// (tests/data/fake_cmake.sh) that "builds" a copy of a foreign module.
//
// argv[1] = repository root, argv[2] = a module built for another ABI.
#include "check.h"
#include "design/DesignBuilder.h"
#include "design/DesignModule.h"
#include "design/Process.h"
#include "design/VerilatorJson.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace vb::design;

namespace {
fs::path repo;
fs::path scratch;
fs::path foreignModule;

void write(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

std::string read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}

DesignBuild build(std::vector<std::string> sources, std::string top = {},
                  std::vector<std::string> includes = {}, const Toolchain& tools = Toolchain::fromBuild(),
                  const fs::path& cache = scratch / "cache") {
  DesignRequest request;
  request.sources = std::move(sources);
  request.top = std::move(top);
  request.includeDirs = std::move(includes);
  request.cacheRoot = cache;
  const DesignBuild result = buildDesign(request, tools);
  if (!result.ok && !result.buildLog.empty()) std::fprintf(stderr, "%s\n", result.buildLog.c_str());
  return result;
}

// The fake CMake: fast builds whose module is a copy of the foreign one.
Toolchain fakeTools() {
  Toolchain tools = Toolchain::fromBuild();
  tools.cmake = scratch / "fake_cmake.sh";
  return tools;
}
const fs::path fakeCache() { return scratch / "fake cache"; }
DesignBuild fakeBuild(std::vector<std::string> sources, std::string top = {}) {
  return build(std::move(sources), std::move(top), {}, fakeTools(), fakeCache());
}

uint64_t peek(vb::SimEngine& engine, const char* name) { return engine.peek(engine.lookup(name)); }

void counterRunsCachesAndRebuilds() {
  const std::string source = (repo / "examples/counter.v").string();
  const DesignBuild first = build({source});
  if (!first.ok) std::fprintf(stderr, "%s\n%s\n%s\n", first.error.c_str(), first.diagnostics.c_str(), first.buildLog.c_str());
  CHECK(first.ok);
  CHECK(!first.reused);
  CHECK(first.top == "counter");
  CHECK_EQ(first.files.size(), 1);
  CHECK(first.files[0] == fs::canonical(source).string());
  CHECK(first.diagnostics.empty());  // the example is -Wall clean

  std::string error;
  const auto module = DesignModule::load(first.module, error);
  CHECK(module);
  auto engine = module->createEngine("counter", "clk", error);
  CHECK(engine);
  // The counter adds sw to led on every rising edge.
  CHECK_EQ(engine->ports().size(), 4);
  engine->poke(engine->lookup("sw"), 3);
  engine->step(10);
  CHECK_EQ(peek(*engine, "led"), 30);
  CHECK_EQ(engine->now(), 10);
  CHECK_EQ(peek(*engine, "counter.count"), 30);  // hierarchical watch, read-only
  // A wrong clock name is the engine's own error, not a crash.
  CHECK(!module->createEngine("counter", "clock", error));
  CHECK(error.find("clock") != std::string::npos);

  const DesignBuild again = build({source});
  CHECK(again.ok && again.reused);
  CHECK(again.module == first.module);

  // Any change to the sources is a different module.
  const fs::path copy = scratch / "edited/counter.v";
  write(copy, "// edited\n" + read(source));
  const DesignBuild edited = build({copy.string()});
  CHECK(edited.ok && !edited.reused);
  CHECK(edited.module != first.module);
}

void diagnosticsAndTops() {
  // Width mismatches that Vivado accepts: warnings shown, and the design runs.
  write(scratch / "widthy.v",
        "module widthy(input wire clk, input wire [3:0] sw, output reg [15:0] led);\n"
        "  reg [7:0] count;\n"
        "  always @(posedge clk) count <= count + sw;\n"
        "  always @(*) led = count;\n"
        "endmodule\n");
  const DesignBuild widthy = build({(scratch / "widthy.v").string()});
  CHECK(widthy.ok);
  CHECK(widthy.diagnostics.find("%Warning-WIDTH") != std::string::npos);
  std::string error;
  auto engine = DesignModule::load(widthy.module, error)->createEngine("widthy", "clk", error);
  CHECK(engine);
  engine->poke(engine->lookup("sw"), 15);
  engine->step(20);  // 300 wraps at 8 bits: 44
  CHECK_EQ(peek(*engine, "led"), 44);

  // Warnings from Verilator's late stages, which its front end alone never
  // reaches: an inferred latch and a combinational loop.
  write(scratch / "latch.v", "module latch(input wire clk, input wire [15:0] sw, output reg [15:0] led);\n"
                             "  always @(*) if (sw[0]) led = sw;\n"
                             "endmodule\n");
  const DesignBuild latch = fakeBuild({(scratch / "latch.v").string()});
  CHECK(latch.ok);
  CHECK(latch.diagnostics.find("%Warning-LATCH: " + (scratch / "latch.v").string() + ":2:3") != std::string::npos);
  write(scratch / "loop.v", "module loop(input wire clk, input wire [15:0] sw, output wire [15:0] led);\n"
                            "  wire [3:0] a;\n"
                            "  assign a[0] = sw[0];\n"
                            "  assign a[3:1] = a[2:0] ^ sw[3:1];\n"
                            "  assign led = {12'b0, a};\n"
                            "endmodule\n");
  const DesignBuild loop = fakeBuild({(scratch / "loop.v").string()});
  CHECK(loop.ok);
  CHECK(loop.diagnostics.find("%Warning-UNOPTFLAT") != std::string::npos);

  // Every source is Verilog-2005 (R5), whatever its extension.
  for (const char* name : {"sv.v", "sv.sv"}) {
    write(scratch / name, "module sv(input logic clk, output logic [15:0] led);\n"
                          "  always_ff @(posedge clk) led <= led + 1;\n"
                          "endmodule\n");
    const DesignBuild sv = fakeBuild({(scratch / name).string()});
    CHECK(!sv.ok && sv.error == "Verilator rejected the design");
    CHECK(sv.diagnostics.find("%Error: " + (scratch / name).string() + ":2:13: syntax error") != std::string::npos);
  }

  // A syntax error: Verilator's own message, verbatim, and no module.
  write(scratch / "broken.v", "module broken(input wire clk, output reg led);\n"
                              "  always @(posedge clk) led <= ~led\nendmodule\n");
  const DesignBuild broken = build({(scratch / "broken.v").string()});
  CHECK(!broken.ok);
  CHECK(broken.diagnostics.find("%Error: " + (scratch / "broken.v").string() + ":3:1: syntax error")
        != std::string::npos);
  CHECK(broken.error == "Verilator rejected the design");

  // Several tops need --top; an unknown top is Verilator's error.
  write(scratch / "multi.v", "module ta(input wire clk, output wire x); assign x = clk; endmodule\n"
                             "module tb(input wire clk, output wire y); assign y = ~clk; endmodule\n");
  const DesignBuild multi = build({(scratch / "multi.v").string()});
  CHECK(!multi.ok);
  CHECK(multi.error == "the design has several top-level modules (ta, tb); choose one with --top NAME");
  const DesignBuild chosen = build({(scratch / "multi.v").string()}, "tb");
  CHECK(chosen.ok && chosen.top == "tb");
  auto tb = DesignModule::load(chosen.module, error)->createEngine("tb", "clk", error);
  CHECK(tb && tb->lookup("y") != vb::kNoSignal);
  const DesignBuild unknown = build({(scratch / "multi.v").string()}, "tc");
  CHECK(!unknown.ok && unknown.diagnostics.find("tc") != std::string::npos);

  // Missing inputs are named.
  CHECK(build({(scratch / "nope.v").string()}).error == "cannot read source '" + (scratch / "nope.v").string() + "'");
  CHECK(build({}).error == "no Verilog source files given");
  CHECK(build({(scratch / "multi.v").string()}, "ta", {(scratch / "nodir").string()}).error
        == "cannot read include directory '" + (scratch / "nodir").string() + "'");
}

void includesAndMemoryFiles() {
  write(scratch / "inc/defs.vh", "`define STEP 7\n");
  write(scratch / "withinc.v", "`include \"defs.vh\"\n"
                               "module withinc(input wire clk, output reg [15:0] led);\n"
                               "  always @(posedge clk) led <= led + `STEP;\n"
                               "endmodule\n");
  const DesignBuild first = build({(scratch / "withinc.v").string()}, {}, {(scratch / "inc").string()});
  CHECK(first.ok);
  CHECK_EQ(first.files.size(), 2);  // the source and the included file
  std::string error;
  auto engine = DesignModule::load(first.module, error)->createEngine("withinc", "clk", error);
  engine->step(3);
  CHECK_EQ(peek(*engine, "led"), 21);
  // Editing only the included file is a different module.
  write(scratch / "inc/defs.vh", "`define STEP 9\n");
  const DesignBuild second = build({(scratch / "withinc.v").string()}, {}, {(scratch / "inc").string()});
  CHECK(second.ok && !second.reused && second.module != first.module);
  auto nine = DesignModule::load(second.module, error)->createEngine("withinc", "clk", error);
  nine->step(3);
  CHECK_EQ(peek(*nine, "led"), 27);

  // $readmemh paths are relative to the working directory, as for any
  // simulation binary: here the fixture's own directory.
  const fs::path data = repo / "tests/data";
  const DesignBuild edges = build({(data / "engine_edges.v").string()});
  CHECK(edges.ok);
  const fs::path previous = fs::current_path();
  fs::current_path(data);
  auto memory = DesignModule::load(edges.module, error)->createEngine("engine_edges", "clk", error);
  fs::current_path(previous);
  CHECK(memory);
  CHECK_EQ(peek(*memory, "mem0"), 0x96);
  CHECK_EQ(peek(*memory, "mem1"), 0x3b);
}

// Paths CMake cannot build from (a space, a semicolon, Hangul); `include
// files found through the working directory, through relative paths from an
// -I directory, by names that are themselves awkward, and by absolute path;
// and a cache root with a space.
void awkwardPaths() {
  const fs::path dir = scratch / "my designs; 설계";
  write(dir / "local.vh", "`define STEP 3\n");
  write(dir / "inc lude/shared.vh", "`define SHIFT 1\n");
  write(dir / "up.vh", "`define BASE 16'd100\n");
  write(dir / "설정.vh", "`define KOREAN 16'd10\n");
  write(dir / "common defs/more.vh", "`define MORE 16'd20\n");
  write(scratch / "abs/abs.vh", "`define ABS 16'd1\n");
  // Includes through symbolic links: a file link, a directory link elsewhere,
  // and `..` after that link, which goes to the link target's parent (a
  // decoy in this directory has another value).
  write(dir / "targets/linked.vh", "`define LINKED 16'd1000\n");
  fs::create_symlink("targets/linked.vh", dir / "linked.vh");
  write(scratch / "elsewhere/targets/deep.vh", "`define DEEP 16'd2000\n");
  write(scratch / "elsewhere/outside.vh", "`define OUT 16'd5\n");
  write(dir / "outside.vh", "`define OUT 16'd7\n");
  fs::create_symlink(scratch / "elsewhere/targets", dir / "lib");
  write(dir / "top file.v", "`include \"local.vh\"\n"
                            "`include \"shared.vh\"\n"
                            "`include \"../up.vh\"\n"
                            "`include \"설정.vh\"\n"
                            "`include \"../common defs/more.vh\"\n"
                            "`include \"" + (scratch / "abs/abs.vh").string() + "\"\n"
                            "`include \"linked.vh\"\n"
                            "`include \"lib/deep.vh\"\n"
                            "`include \"lib/../outside.vh\"\n"
                            "module odd(input wire clk, input wire btnC, output reg [15:0] led);\n"
                            "  always @(posedge clk)\n"
                            "    led <= btnC ? `BASE + `KOREAN + `MORE + `LINKED + `DEEP + `OUT\n"
                            "               : led + (`STEP << `SHIFT) + `ABS;\n"
                            "endmodule\n");
  const fs::path previous = fs::current_path();
  fs::current_path(dir);  // local.vh is found only here
  const auto odd = [&] {
    return build({"top file.v"}, {}, {"inc lude"}, Toolchain::fromBuild(), scratch / "cache with space");
  };
  const DesignBuild first = odd();
  const DesignBuild again = odd();
  fs::current_path(previous);
  if (!first.ok) std::fprintf(stderr, "%s\n%s\n", first.error.c_str(), first.diagnostics.c_str());
  CHECK(first.ok && !first.reused);
  CHECK(again.ok && again.reused && again.module == first.module);
  const fs::path real = fs::canonical(dir);
  std::vector<std::string> expected{(real / "inc lude/shared.vh").string(), (real / "local.vh").string(),
                                    (real / "top file.v").string(), (real / "up.vh").string(),
                                    (real / "설정.vh").string(), (real / "common defs/more.vh").string(),
                                    fs::canonical(scratch / "abs/abs.vh").string(),
                                    (real / "targets/linked.vh").string(),
                                    fs::canonical(scratch / "elsewhere/targets/deep.vh").string(),
                                    fs::canonical(scratch / "elsewhere/outside.vh").string()};
  std::sort(expected.begin(), expected.end());
  CHECK(first.files == expected);
  std::string error;
  auto engine = DesignModule::load(first.module, error)->createEngine("odd", "clk", error);
  CHECK(engine);
  engine->poke(engine->lookup("btnC"), 1);
  engine->step(1);
  engine->poke(engine->lookup("btnC"), 0);
  engine->step(5);
  CHECK_EQ(peek(*engine, "led"), 3170);  // 100 + 10 + 20 + 1000 + 2000 + 5 + 5 * ((3 << 1) + 1)
}

void foreignModulesAreRefused() {
  std::string error;
  CHECK(!DesignModule::load(foreignModule, error));
  CHECK(error == "the design module was built for a different VirtualBasys build; rebuild "
                 "VirtualBasys or delete the design cache");
  write(scratch / "not-a-module.so", "text");
  CHECK(!DesignModule::load(scratch / "not-a-module.so", error));
  CHECK(error.rfind("cannot load the design module: ", 0) == 0);
}

void jsonReader() {
  // Verilator's octal escapes for bytes outside ASCII.
  const auto files = filesRead(R"({"files":{"a":{"filename":"x","realpath":"/p/\352\263\274.v"},)"
                               R"("b":{"filename":"<built-in>","realpath":"<built-in>"}}})");
  CHECK(files && files->size() == 1);
  CHECK(files->front().found == "x" && files->front().real == "/p/\xea\xb3\xbc.v");
  // Any depth: Verilator nests one level per operand.
  const std::string deep = std::string(100'000, '[') + std::string(100'000, ']');
  const auto tops = topModules(R"({"deep":)" + deep + R"(,"modulesp":[{"name":"t","level":1,"stmtsp":[)"
                               R"({"type":"VAR","name":"clk","direction":"INPUT","x":{"y":[1,"]"]}},)"
                               R"({"type":"VAR","name":"led","direction":"OUTPUT"}]},)"
                               R"({"name":"sub","level":2}]})");
  CHECK(tops && tops->size() == 1);
  CHECK(tops->front().name == "t" && tops->front().inputs == std::vector<std::string>{"clk"});
  CHECK(!topModules(R"({"modulesp":[{"name":"t")"));

  // A real design whose tree is hundreds of levels deep.
  std::string chain = "module chain(input wire clk, input wire [15:0] sw, output reg [15:0] led);\n"
                      "  always @(posedge clk)\n    if (sw == 16'd0) led <= 16'd0;\n";
  for (int i = 1; i < 400; ++i) chain += "    else if (sw == 16'd" + std::to_string(i) + ") led <= 16'd" + std::to_string(i) + ";\n";
  chain += "    else led <= 16'hffff;\nendmodule\n";
  write(scratch / "chain.v", chain);
  const DesignBuild built = fakeBuild({(scratch / "chain.v").string()});
  if (!built.ok) std::fprintf(stderr, "chain: %s\n%s\n", built.error.c_str(), built.buildLog.c_str());
  CHECK(built.ok && built.top == "chain");
}

// What the cache stores, when, and how it heals: with the fake CMake.
void cacheBehaviour() {
  const Toolchain tools = fakeTools();
  write(scratch / "multi2.v", "module ta(input wire clk, output wire x); assign x = clk; endmodule\n"
                              "module tb(input wire clk, output wire y); assign y = ~clk; endmodule\n");
  const std::string multi = (scratch / "multi2.v").string();

  // The top is part of the key.
  const DesignBuild ta = fakeBuild({multi}, "ta");
  CHECK(ta.ok && !ta.reused);
  const DesignBuild tb = fakeBuild({multi}, "tb");
  CHECK(tb.ok && !tb.reused && tb.module != ta.module);
  CHECK(fakeBuild({multi}, "ta").reused);

  // So are the engine and module sources the module is compiled with.
  Toolchain copied = tools;
  copied.sourceDir = scratch / "source copy";
  for (const char* part : {"src/engine/SimEngine.h", "src/engine/SimEngine.cpp",
                           "src/engine/VerilatorEngine.h", "src/engine/VerilatorEngine.cpp",
                           "src/design/ModuleAbi.h", "src/design/ModuleAbi.cmake",
                           "src/design/module/CMakeLists.txt", "src/design/module/factory.cpp"})
    write(copied.sourceDir / part, read(repo / part));
  CHECK(!build({multi}, "ta", {}, copied, fakeCache()).reused);
  CHECK(build({multi}, "ta", {}, copied, fakeCache()).reused);
  for (const char* part : {"src/engine/VerilatorEngine.cpp", "src/design/module/CMakeLists.txt"}) {
    std::ofstream(copied.sourceDir / part, std::ios::app) << "// changed\n";
    CHECK(!build({multi}, "ta", {}, copied, fakeCache()).reused);
  }
  // An engine source edited during the build and put back: not stored.
  ::setenv("VB_FAKE_UNDO", (copied.sourceDir / "src/engine/VerilatorEngine.cpp").c_str(), 1);
  const DesignBuild engineEdited = build({multi}, "tb", {}, copied, fakeCache());
  ::unsetenv("VB_FAKE_UNDO");
  CHECK(!engineEdited.ok);
  CHECK(engineEdited.error == "a design file changed during the build, so the module was not stored; run again");

  // The caller checks the top and its inputs before anything is built.
  DesignRequest request;
  request.sources = {multi};
  request.top = "tb";
  request.cacheRoot = fakeCache();
  std::vector<std::string> seen;
  bool started = false;
  request.validate = [&](const std::string& top, const std::vector<std::string>& inputs) {
    seen = inputs;
    return top == "tb" ? "no, thanks" : "";
  };
  request.onBuildStart = [&](const std::string&) { started = true; };
  const DesignBuild refused = buildDesign(request, tools);
  CHECK(!refused.ok && refused.error == "no, thanks" && !started);
  CHECK(seen == std::vector<std::string>{"clk"});

  // A file edited during the build: nothing is stored under the old key.
  write(scratch / "edit.v", "module edit(input wire clk, output wire x); assign x = clk; endmodule\n");
  ::setenv("VB_FAKE_TOUCH", (scratch / "edit.v").c_str(), 1);
  const DesignBuild raced = fakeBuild({(scratch / "edit.v").string()});
  ::unsetenv("VB_FAKE_TOUCH");
  CHECK(!raced.ok);
  CHECK(raced.error == "a design file changed during the build, so the module was not stored; run again");
  CHECK(!fs::exists(raced.module.parent_path()));
  const DesignBuild settled = fakeBuild({(scratch / "edit.v").string()});
  CHECK(settled.ok && !settled.reused && settled.module != raced.module);
  // Also an edit that is undone during the build (its contents are the
  // checked ones again, but the build may have read the edit), and either
  // edit to the original of a copied design, such as an `include by
  // absolute path, which the build reads directly.
  write(scratch / "undo.v", "module undo(input wire clk, output wire x); assign x = clk; endmodule\n");
  write(scratch / "copied dir/copied.v", "module copied(input wire clk, output wire x); assign x = clk; endmodule\n");
  for (const fs::path file : {scratch / "undo.v", scratch / "copied dir/copied.v"}) {
    for (const char* edit : {"VB_FAKE_UNDO", "VB_FAKE_TOUCH"}) {
      const std::string before = read(file);
      ::setenv(edit, file.c_str(), 1);
      const DesignBuild edited = fakeBuild({file.string()});
      ::unsetenv(edit);
      CHECK(!edited.ok);
      CHECK(edited.error == "a design file changed during the build, so the module was not stored; run again");
      CHECK(!fs::exists(edited.module.parent_path()));
      if (std::string(edit) == "VB_FAKE_UNDO") CHECK(read(file) == before);
    }
  }
  // An undo that also puts the modification time back, to the nanosecond:
  // only the status-change time tells.
  write(scratch / "ctime.v", "module ctime(input wire clk, output wire x); assign x = clk; endmodule\n");
  {
    DesignRequest sneaky;
    sneaky.sources = {(scratch / "ctime.v").string()};
    sneaky.cacheRoot = fakeCache();
    sneaky.onBuildStart = [&](const std::string&) {
      const fs::path file = scratch / "ctime.v";
      struct stat before {};
      CHECK(::stat(file.c_str(), &before) == 0);
      const std::string text = read(file);
      { std::ofstream(file, std::ios::binary | std::ios::in | std::ios::out) << "// edited"; }
      { std::ofstream(file, std::ios::binary | std::ios::in | std::ios::out) << text; }
      const timespec times[2] = {before.st_atimespec, before.st_mtimespec};
      CHECK(::utimensat(AT_FDCWD, file.c_str(), times, 0) == 0);
      struct stat after {};
      CHECK(::stat(file.c_str(), &after) == 0);
      CHECK(after.st_ino == before.st_ino && after.st_size == before.st_size);
      CHECK(after.st_mtimespec.tv_sec == before.st_mtimespec.tv_sec
            && after.st_mtimespec.tv_nsec == before.st_mtimespec.tv_nsec);
    };
    const DesignBuild restored = buildDesign(sneaky, tools);
    CHECK(!restored.ok);
    CHECK(restored.error == "a design file changed during the build, so the module was not stored; run again");
  }
  // An `include by an absolute path that is not plain is refused before any
  // build: the build would read it where it is.
  write(scratch / "abs dir/x.vh", "`define X 1\n");
  write(scratch / "absinc.v", "`include \"" + (scratch / "abs dir/x.vh").string() + "\"\n"
                              "module absinc(input wire clk, output wire x); assign x = clk; endmodule\n");
  {
    DesignRequest absolute;
    absolute.sources = {(scratch / "absinc.v").string()};
    absolute.cacheRoot = fakeCache();
    bool started = false;
    absolute.onBuildStart = [&](const std::string&) { started = true; };
    const DesignBuild refusedInclude = buildDesign(absolute, tools);
    CHECK(!refusedInclude.ok && !started);
    CHECK(refusedInclude.error == "'" + (scratch / "abs dir/x.vh").string() + "' is included by an absolute "
          "path with, or leading to, spaces, semicolons or characters outside ASCII, which the build cannot "
          "use; include it through an -I directory instead");
  }
  // Also when the absolute path is plain but leads there through a link.
  fs::create_symlink(scratch / "abs dir", scratch / "plainlink");
  write(scratch / "abslink.v", "`include \"" + (scratch / "plainlink/x.vh").string() + "\"\n"
                               "module abslink(input wire clk, output wire x); assign x = clk; endmodule\n");
  {
    const DesignBuild refusedLink = fakeBuild({(scratch / "abslink.v").string()});
    CHECK(!refusedLink.ok);
    CHECK(refusedLink.error.rfind("'" + (scratch / "plainlink/x.vh").string() + "' is included by an absolute "
                                  "path with, or leading to, spaces", 0) == 0);
  }
  // A Verilator installation that forwards to another one (its standard
  // library then lies outside it): Verilator's own pseudo-files, such as
  // <verilated_std>, are not design files to find or mirror.
  {
    Toolchain forwarding = tools;
    forwarding.verilatorDir = scratch / "forwarding";
    forwarding.verilator = scratch / "forwarding/bin/verilator";
    write(forwarding.verilator, "#!/bin/sh\nexec '" + tools.verilator.string() + "' \"$@\"\n");
    fs::permissions(forwarding.verilator, fs::perms::owner_all);
    write(scratch / "forwarded.v", "module forwarded(input wire clk, output wire x); assign x = clk; endmodule\n");
    const DesignBuild forwarded = build({(scratch / "forwarded.v").string()}, {}, {}, forwarding, fakeCache());
    if (!forwarded.ok) std::fprintf(stderr, "forwarded: %s\n", forwarded.error.c_str());
    CHECK(forwarded.ok);
  }
  // A symbolic link on the way to an included file, pointed elsewhere and
  // back during the build: not stored.
  write(scratch / "relink/a.vh", "`define V 1\n");
  write(scratch / "relink/b.vh", "`define V 9\n");
  fs::create_symlink("a.vh", scratch / "relink/step.vh");
  write(scratch / "relink/relink.v", "`include \"" + (scratch / "relink/step.vh").string() + "\"\n"
                                     "module relink(input wire clk, output wire x); assign x = clk; endmodule\n");
  ::setenv("VB_FAKE_RELINK", (scratch / "relink/step.vh").c_str(), 1);
  ::setenv("VB_FAKE_RELINK_TO", "b.vh", 1);
  const DesignBuild relinked = fakeBuild({(scratch / "relink/relink.v").string()});
  ::unsetenv("VB_FAKE_RELINK");
  CHECK(!relinked.ok);
  CHECK(relinked.error == "a design file changed during the build, so the module was not stored; run again");
  CHECK(fs::read_symlink(scratch / "relink/step.vh") == "a.vh");
  CHECK(fakeBuild({(scratch / "relink/relink.v").string()}).ok);
  // The build may read only the files the check read.
  write(scratch / "deps.v", "module deps(input wire clk, output wire x); assign x = clk; endmodule\n");
  write(scratch / "stray.vh", "`define STRAY 1\n");
  ::setenv("VB_FAKE_DEPS", fs::canonical(scratch / "deps.v").c_str(), 1);
  CHECK(fakeBuild({(scratch / "deps.v").string()}, "deps").ok);
  // ... by path, identity and size.
  ::setenv("VB_FAKE_DEPS_STAT", "1 2", 1);
  write(scratch / "deps.v", "module deps(input wire clk, output wire z); assign z = clk; endmodule\n");
  const DesignBuild other = fakeBuild({(scratch / "deps.v").string()});
  ::unsetenv("VB_FAKE_DEPS_STAT");
  CHECK(!other.ok);
  CHECK(other.error == "the build read a different '" + fs::canonical(scratch / "deps.v").string()
                           + "' than the check did, so the module was not stored; run again");
  // The Verilator program may live anywhere (an installation whose
  // bin/verilator_bin is a wrapper), but only the program: a header named
  // like it, or whose name merely starts like it, is checked like any other.
  write(scratch / "wrapper/bin/verilator_bin", "#!/bin/sh\n");
  fs::permissions(scratch / "wrapper/bin/verilator_bin", fs::perms::owner_all);
  ::setenv("VB_FAKE_DEPS", (scratch / "wrapper/bin/verilator_bin").c_str(), 1);
  write(scratch / "deps.v", "module deps(input wire clk, output wire w); assign w = clk; endmodule\n");
  CHECK(fakeBuild({(scratch / "deps.v").string()}).ok);
  write(scratch / "headers/verilator_bin", "`define STEP 9\n");
  ::setenv("VB_FAKE_DEPS", (scratch / "headers/verilator_bin").c_str(), 1);
  write(scratch / "deps.v", "module deps(input wire clk, output wire u); assign u = clk; endmodule\n");
  const DesignBuild named = fakeBuild({(scratch / "deps.v").string()});
  CHECK(!named.ok);
  CHECK(named.error == "the build read '" + (scratch / "headers/verilator_bin").string()
                           + "', which the check did not, so the module was not stored; run again");
  write(scratch / "verilator_bin_defs.vh", "`define STEP 9\n");
  fs::permissions(scratch / "verilator_bin_defs.vh", fs::perms::owner_all);  // even executable
  ::setenv("VB_FAKE_DEPS", (scratch / "verilator_bin_defs.vh").c_str(), 1);
  write(scratch / "deps.v", "module deps(input wire clk, output wire v); assign v = clk; endmodule\n");
  const DesignBuild lookalike = fakeBuild({(scratch / "deps.v").string()});
  CHECK(!lookalike.ok);
  CHECK(lookalike.error == "the build read '" + (scratch / "verilator_bin_defs.vh").string()
                               + "', which the check did not, so the module was not stored; run again");
  ::setenv("VB_FAKE_DEPS", fs::canonical(scratch / "deps.v").c_str(), 1);
  ::setenv("VB_FAKE_DEPS", (scratch / "stray.vh").c_str(), 1);
  write(scratch / "deps.v", "module deps(input wire clk, output wire y); assign y = clk; endmodule\n");
  const DesignBuild stray = fakeBuild({(scratch / "deps.v").string()});
  ::unsetenv("VB_FAKE_DEPS");
  CHECK(!stray.ok);
  CHECK(stray.error == "the build read '" + (scratch / "stray.vh").string()
                           + "', which the check did not, so the module was not stored; run again");
  CHECK(!fs::exists(stray.module.parent_path()));

  // Build failures report only the failing step's errors.
  write(scratch / "fails.v", "module fails(input wire clk, output wire x); assign x = clk; endmodule\n");
  for (const std::string step : {"configure", "build"}) {
    ::setenv("VB_FAKE_FAIL", step.c_str(), 1);
    const DesignBuild failed = fakeBuild({(scratch / "fails.v").string()});
    CHECK(!failed.ok && failed.error == "the design module failed to build");
    CHECK(failed.buildLog == "fake " + step + " failure\n");
    CHECK(!fs::exists(failed.module));
  }
  ::unsetenv("VB_FAKE_FAIL");
  Toolchain missing = tools;
  missing.cmake = "/nonexistent/cmake";
  const DesignBuild noCmake = build({(scratch / "fails.v").string()}, {}, {}, missing, scratch / "other cache");
  CHECK(!noCmake.ok && noCmake.error == "cannot run CMake (/nonexistent/cmake)");
  missing.verilator = "/nonexistent/verilator";
  const DesignBuild noVerilator = build({multi}, "ta", {}, missing, fakeCache());
  CHECK(!noVerilator.ok && noVerilator.error == "cannot run Verilator (/nonexistent/verilator)");
  CHECK(noVerilator.diagnostics.empty());

  // The build sees neither the user's compiler flags nor VERILATOR_ROOT, and
  // plain paths reach it unchanged, the working directory last.
  const fs::path args = scratch / "fake-args.txt";
  ::setenv("VB_FAKE_ARGS", args.c_str(), 1);
  ::setenv("CXXFLAGS", "-DBOGUS", 1);
  ::setenv("MACOSX_DEPLOYMENT_TARGET", "99.0", 1);
  write(scratch / "flags.v", "module flags(input wire clk, output wire x); assign x = clk; endmodule\n");
  CHECK(fakeBuild({(scratch / "flags.v").string()}).ok);
  ::unsetenv("CXXFLAGS");
  ::unsetenv("MACOSX_DEPLOYMENT_TARGET");
  const std::string seenArgs = read(args);
  CHECK(seenArgs.find("CXXFLAGS=unset") != std::string::npos);
  CHECK(seenArgs.find("CXXFLAGS=-DBOGUS") == std::string::npos);
  CHECK(seenArgs.find("MACOSX_DEPLOYMENT_TARGET=unset") != std::string::npos);
  CHECK(seenArgs.find("-DVB_DESIGN_SOURCES=" + fs::canonical(scratch / "flags.v").string() + "\n")
        != std::string::npos);
  CHECK(seenArgs.find("-DVB_DESIGN_INCLUDES=" + fs::canonical(fs::current_path()).string() + "\n")
        != std::string::npos);
  // A path with a space reaches it as a copy with plain names.
  fs::remove(args);
  write(scratch / "spaced dir/flags.v", read(scratch / "flags.v"));
  CHECK(fakeBuild({(scratch / "spaced dir/flags.v").string()}).ok);
  const std::string spacedArgs = read(args);
  const auto sourcesAt = spacedArgs.find("-DVB_DESIGN_SOURCES=");
  CHECK(sourcesAt != std::string::npos);
  const std::string spacedSource = spacedArgs.substr(sourcesAt, spacedArgs.find('\n', sourcesAt) - sourcesAt);
  CHECK(spacedSource.find("/virtualbasys-build-") != std::string::npos);
  CHECK(spacedSource.find(' ') == std::string::npos && spacedSource.find(".v") != std::string::npos);
  ::unsetenv("VB_FAKE_ARGS");

  // Damaged entries heal: one without its module, and one replaced on request
  // (the app rebuilds when a cached module does not load).
  const DesignBuild good = fakeBuild({(scratch / "flags.v").string()});
  CHECK(good.ok && good.reused);
  fs::remove(good.module);
  const DesignBuild healed = fakeBuild({(scratch / "flags.v").string()});
  CHECK(healed.ok && !healed.reused && fs::is_regular_file(healed.module));
  write(healed.module, "truncated");
  CHECK(fakeBuild({(scratch / "flags.v").string()}).reused);
  request = {};
  request.sources = {(scratch / "flags.v").string()};
  request.cacheRoot = fakeCache();
  request.rebuild = true;
  const DesignBuild rebuilt = buildDesign(request, tools);
  CHECK(rebuilt.ok && !rebuilt.reused);
  CHECK_EQ(fs::file_size(rebuilt.module), fs::file_size(foreignModule));

  // A read-only cache still serves what it has, and refuses a new module
  // before building it. An entry it cannot read is an error, not a rebuild.
  const auto startsBuild = [&](const fs::path& source) {
    DesignRequest probe;
    probe.sources = {source.string()};
    probe.cacheRoot = fakeCache();
    bool started = false;
    probe.onBuildStart = [&](const std::string&) { started = true; };
    const DesignBuild result = buildDesign(probe, tools);
    return std::make_pair(result, started);
  };
  write(scratch / "late2.v", "module late2(input wire clk, output wire x); assign x = clk; endmodule\n");
  ::chmod(fakeCache().c_str(), 0555);
  const DesignBuild readOnly = fakeBuild({(scratch / "flags.v").string()});
  const auto [unwritable, unwritableStarted] = startsBuild(scratch / "late2.v");
  ::chmod(fakeCache().c_str(), 0755);
  CHECK(readOnly.ok && readOnly.reused);
  CHECK(!unwritable.ok && !unwritableStarted);
  CHECK(unwritable.error == "cannot write to the design cache '" + fakeCache().string() + "'");
  ::chmod(readOnly.module.parent_path().c_str(), 0);
  const auto [unreadable, unreadableStarted] = startsBuild(scratch / "flags.v");
  ::chmod(readOnly.module.parent_path().c_str(), 0755);
  CHECK(!unreadable.ok && !unreadableStarted);
  CHECK(unreadable.error.rfind("cannot read the design cache entry '" + readOnly.module.parent_path().string()
                                   + "': ", 0) == 0);

  // Work directories of processes that no longer run are cleaned up.
  const fs::path temp = fs::canonical(fs::temp_directory_path());
  fs::create_directories(fakeCache() / ".stage-99999-0");
  fs::create_directories(temp / "virtualbasys-build-99999-0");
  CHECK(fakeBuild({(scratch / "flags.v").string()}).ok);
  CHECK(!fs::exists(fakeCache() / ".stage-99999-0"));
  CHECK(!fs::exists(temp / "virtualbasys-build-99999-0"));
}

// Last: cancellation lasts for the rest of the process.
void interruption() {
  cancelProcesses();
  CHECK(cancelRequested());
  write(scratch / "late.v", "module late(input wire clk, output wire x); assign x = clk; endmodule\n");
  const DesignBuild late = fakeBuild({(scratch / "late.v").string()});
  CHECK(!late.ok && late.interrupted && late.error == "interrupted");
  CHECK(late.module.empty());
}
}  // namespace

int main(int argc, char** argv) {
  CHECK(argc == 3);
  repo = argv[1];
  foreignModule = argv[2];
  scratch = fs::temp_directory_path() / ("vb-design-loader-" + std::to_string(::getpid()));
  fs::remove_all(scratch);
  fs::create_directories(scratch);
  fs::copy_file(repo / "tests/data/fake_cmake.sh", scratch / "fake_cmake.sh");
  fs::permissions(scratch / "fake_cmake.sh", fs::perms::owner_all);
  ::setenv("VB_FAKE_MODULE", foreignModule.c_str(), 1);

  jsonReader();
  counterRunsCachesAndRebuilds();
  diagnosticsAndTops();
  includesAndMemoryFiles();
  awkwardPaths();
  foreignModulesAreRefused();
  cacheBehaviour();
  interruption();
  // Work directories never outlive a build.
  for (const fs::path cache : {scratch / "cache", scratch / "cache with space", fakeCache()})
    for (const auto& entry : fs::directory_iterator(cache))
      CHECK(entry.path().filename().string().front() != '.');
  const std::string mine = "-" + std::to_string(::getpid()) + "-";
  for (const auto& entry : fs::directory_iterator(fs::temp_directory_path())) {
    const std::string name = entry.path().filename().string();
    CHECK(!(name.rfind("virtualbasys-", 0) == 0 && name.find(mine) != std::string::npos));
  }
  fs::remove_all(scratch);
  std::puts("test_design_loader: PASS");
  return 0;
}
