#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace vb::design {

// The tools and source tree a design module is built with. fromBuild() uses
// the paths this program was configured with; VB_VERILATOR_ROOT (a Verilator
// installation, for both the check and the build) and VB_CMAKE override them.
struct Toolchain {
  std::filesystem::path verilator;     // the verilator driver
  std::filesystem::path verilatorDir;  // for find_package(verilator)
  std::filesystem::path cmake;
  std::filesystem::path cxx;           // the compiler this program was built with
  std::filesystem::path sourceDir;     // this repository: engine and module sources
  static Toolchain fromBuild();
};

struct DesignRequest {
  std::vector<std::string> sources;      // Verilog files, as given
  std::string top;                       // empty: the design's only top module
  std::vector<std::string> includeDirs;  // `include search path, before the working directory
  std::filesystem::path cacheRoot;       // empty: defaultCacheRoot()
  bool rebuild = false;                  // discard a cached module (one that failed to load)
  // Called once the top module is known, before the cache is used, with its
  // input ports. A non-empty result stops the build with that error.
  std::function<std::string(const std::string& top, const std::vector<std::string>& inputs)> validate;
  // Called just before a module is built (not on a cache hit), with the top.
  std::function<void(const std::string& top)> onBuildStart;
};

struct DesignBuild {
  bool ok = false;
  std::string top;                  // the top module used
  std::vector<std::string> files;   // every file Verilator read (real paths, sorted)
  std::filesystem::path module;     // the loadable design module
  bool reused = false;              // taken from the cache, not built now
  bool interrupted = false;         // cancelProcesses() was called; nothing was stored
  std::string diagnostics;          // Verilator's own messages, verbatim
  std::string buildLog;             // the failing build step's messages
  std::string error;                // why it failed, beyond Verilator's messages
};

// $VB_DESIGN_CACHE, or ~/Library/Caches/VirtualBasys/designs.
std::filesystem::path defaultCacheRoot();

// Checks the design with Verilator every time (its messages are always
// returned), then reuses the cached module for exactly these sources, top,
// include directories, tools and engine sources, or builds and caches it.
// The build runs in the system's temporary directory. A module is stored only
// when every file it was built from is unchanged at the end of the build.
DesignBuild buildDesign(const DesignRequest& request, const Toolchain& tools);

}  // namespace vb::design
