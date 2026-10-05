#include "design/DesignBuilder.h"

#include "design/ModuleAbi.h"
#include "design/Process.h"
#include "design/VerilatorJson.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string_view>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace vb::design {
namespace fs = std::filesystem;

namespace {
// Work directories: checks and builds in the temporary directory, staged
// entries in the cache root (published from there with one rename).
constexpr const char* kCheckPrefix = "virtualbasys-check-";
constexpr const char* kBuildPrefix = "virtualbasys-build-";
constexpr const char* kStagePrefix = ".stage-";
constexpr const char* kStalePrefix = ".stale-";

// Verilator's flags for a user design, as in the build (src/design/module):
// as Vivado reads them, .sv sources are SystemVerilog and every other source
// is Verilog-2005 (R5), and warnings are shown without stopping the design.
const char* const kFlags[] = {"--public-flat-rw", "--timescale", "1ns/1ns", "-Wno-fatal",
                              "--default-language", "1364-2005", "+1800-2023ext+sv"};

bool readFile(const fs::path& path, std::string& text) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream contents;
  contents << in.rdbuf();
  text = contents.str();
  return true;
}

std::string readText(const fs::path& path) {
  std::string text;
  readFile(path, text);
  return text;
}

std::string environment(const char* name, const std::string& fallback) {
  const char* value = std::getenv(name);
  return value && *value ? std::string(value) : fallback;
}

// Characters that CMake's verilate() and Make pass through unquoted. A design
// with any other path is built from copies (see build()).
bool plainPath(std::string_view path) {
  return std::all_of(path.begin(), path.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
        || std::string_view("/._+-").find(c) != std::string_view::npos;
  });
}

// Two 64-bit FNV-1a lanes: a cache key, not a security boundary.
class Fingerprint {
public:
  void add(std::string_view bytes) {
    for (const unsigned char c : bytes) {
      a_ = (a_ ^ c) * 0x100000001b3ull;
      b_ = (b_ ^ c) * 0x100000001b3ull;
    }
    // Separate fields, so "ab"+"c" and "a"+"bc" differ.
    a_ = (a_ ^ 0xff) * 0x100000001b3ull;
    b_ = (b_ ^ 0xfe) * 0x100000001b3ull;
  }
  void addFile(const fs::path& path) {
    add(path.string());
    add(readText(path));
  }
  std::string hex() const {
    char text[33];
    std::snprintf(text, sizeof text, "%016llx%016llx", static_cast<unsigned long long>(a_),
                  static_cast<unsigned long long>(b_));
    return text;
  }

private:
  uint64_t a_ = 0xcbf29ce484222325ull;
  uint64_t b_ = 0x84222325cbf29ce4ull;
};

// One file's contents, fingerprinted; empty if it cannot be read.
std::string contentOf(const fs::path& path) {
  std::string text;
  if (!readFile(path, text)) return {};
  Fingerprint content;
  content.add(text);
  return content.hex();
}

// The design's contents, for the key: each real path with its contents.
std::string contentsOf(const std::map<std::string, std::string>& content) {
  Fingerprint contents;
  for (const auto& [file, hash] : content) {
    contents.add(file);
    contents.add(hash);
  }
  return contents.hex();
}

// Follows `path` (absolute) as the kernel resolves it, calling
// visit(directory, name, link) for each component it steps through, with
// `directory` the real directory holding it; symbolic links are followed,
// nested ones too, and `..` goes to the real parent. Returns the real path.
fs::path walk(const fs::path& path,
              const std::function<void(const fs::path& directory, const std::string& name, bool link)>& visit) {
  std::deque<std::string> parts;
  for (const auto& part : path.relative_path()) parts.push_back(part.string());
  fs::path current = "/";
  for (int links = 0; !parts.empty();) {
    const std::string part = parts.front();
    parts.pop_front();
    if (part.empty() || part == ".") continue;
    if (part == "..") {
      current = current.parent_path();
      continue;
    }
    const fs::path next = current / part;
    const bool link = fs::is_symlink(fs::symlink_status(next));
    visit(current, part, link);
    if (!link) {
      current = next;
      continue;
    }
    if (++links > 40)
      throw fs::filesystem_error("too many symbolic links", next,
                                 std::make_error_code(std::errc::too_many_symbolic_link_levels));
    const fs::path target = fs::read_symlink(next);
    if (target.is_absolute()) current = "/";
    std::vector<std::string> inner;
    for (const auto& piece : target.relative_path()) inner.push_back(piece.string());
    parts.insert(parts.begin(), inner.begin(), inner.end());
  }
  return current;
}

void addStat(Fingerprint& state, const struct stat& info) {
#ifdef __APPLE__
  const timespec modified = info.st_mtimespec, changed = info.st_ctimespec;
#else
  const timespec modified = info.st_mtim, changed = info.st_ctim;
#endif
  for (const long long field : {static_cast<long long>(info.st_dev), static_cast<long long>(info.st_ino),
                                static_cast<long long>(info.st_size), static_cast<long long>(modified.tv_sec),
                                static_cast<long long>(modified.tv_nsec), static_cast<long long>(changed.tv_sec),
                                static_cast<long long>(changed.tv_nsec)})
    state.add(std::to_string(field));
}

// What any write to the design's files changes, even one that puts their
// contents back: for each real file (and each other file the module is built
// from), its identity, size, and modification and status-change times (the
// last cannot be set back); for each name a file was found by, every symbolic
// link on its way, so a link pointed elsewhere and back is a change too.
// Empty if one cannot be read.
std::string stateOf(const std::vector<fs::path>& files, const std::vector<fs::path>& found) {
  Fingerprint state;
  try {
    for (const auto& file : files) {
      struct stat info {};
      if (::stat(file.c_str(), &info) != 0) return {};
      addStat(state, info);
    }
    for (const auto& path : found) {
      state.add(path.string());
      walk(path, [&](const fs::path& directory, const std::string& name, bool link) {
        if (!link) return;
        struct stat info {};
        if (::lstat((directory / name).c_str(), &info) != 0) throw fs::filesystem_error("lstat", directory / name, {});
        state.add((directory / name).string());
        state.add(fs::read_symlink(directory / name).string());
        addStat(state, info);
      });
    }
  } catch (const std::exception&) {
    return {};
  }
  return state.hex();
}

// Identity and size, as Verilator records them for the files it read.
std::optional<std::pair<long long, long long>> identityOf(const fs::path& path) {
  struct stat info {};
  if (::stat(path.c_str(), &info) != 0) return std::nullopt;
  return std::make_pair(static_cast<long long>(info.st_ino), static_cast<long long>(info.st_size));
}

// A plain name for a path component, derived from it: the same component
// always gets the same name.
std::string plainAlias(const fs::path& part) {
  const std::string name = part.string();
  if (plainPath(name)) return name;
  Fingerprint alias;
  alias.add(name);
  const std::string extension = part.extension().string();
  return "x" + alias.hex().substr(0, 16) + (plainPath(extension) ? extension : std::string());
}

// A real path's place under `mirror`: each component under its plain alias,
// so the tree keeps its shape.
fs::path mirrored(const fs::path& mirror, const fs::path& real) {
  fs::path out = mirror;
  for (const auto& part : real.relative_path()) out /= plainAlias(part);
  return out;
}

// Recreates, under `mirror`, everything `path` passes through: real
// directories, each regular file as a copy, and each symbolic link as a link
// whose absolute target is re-based into the mirror. So in the mirror every
// name resolves as it does in the original tree, `..` after a link included.
// Every alias also gets a link under its own name, so `include text that
// spells that name resolves; Verilator records real paths, which are plain.
void mirrorWay(const fs::path& mirror, const fs::path& path) {
  walk(path, [&](const fs::path& directory, const std::string& name, bool link) {
    const fs::path at = mirrored(mirror, directory);
    fs::create_directories(at);
    const std::string alias = plainAlias(name);
    std::error_code missing;
    if (!fs::exists(fs::symlink_status(at / alias, missing))) {
      const fs::path original = directory / name;
      if (link) {
        const fs::path target = fs::read_symlink(original);
        fs::create_symlink(target.is_absolute() ? mirror / target.relative_path() : target, at / alias);
      } else if (fs::is_directory(original)) {
        fs::create_directory(at / alias);
      } else {
        fs::copy_file(original, at / alias);
      }
    }
    if (alias != name && !fs::exists(fs::symlink_status(at / name, missing)))
      fs::create_symlink(alias, at / name);
  });
}

// A file a build's Verilator read, as it recorded it.
struct BuildInput {
  std::string path;  // as found
  long long size = -1;
  long long inode = -1;
};

// The files a build's Verilator read, from its record of them
// (<prefix>__verFiles.dat: one `S size inode ... "hash" "path"` line per
// source it read, the path last and quoted), or nullopt when there is none.
std::optional<std::vector<BuildInput>> buildInputs(const fs::path& binaryDir) {
  std::error_code error;
  for (fs::recursive_directory_iterator it(binaryDir, error), end; !error && it != end; it.increment(error)) {
    if (it->path().filename() != "Vdesign__verFiles.dat") continue;
    std::istringstream lines(readText(it->path()));
    std::vector<BuildInput> inputs;
    for (std::string line; std::getline(lines, line);) {
      if (line.rfind("S ", 0) != 0) continue;
      BuildInput input;
      std::istringstream fields(line.substr(2));
      const auto close = line.rfind('"');
      const auto open = close == std::string::npos || close == 0 ? std::string::npos : line.rfind('"', close - 1);
      if (!(fields >> input.size >> input.inode) || open == std::string::npos) return std::nullopt;
      input.path = line.substr(open + 1, close - open - 1);
      inputs.push_back(input);
    }
    return inputs;
  }
  return std::nullopt;
}

// A private directory <parent>/<prefix><pid>-<n>, removed when done.
class WorkDir {
public:
  WorkDir(const fs::path& parent, const std::string& prefix) {
    for (int attempt = 0; attempt < 1000; ++attempt) {
      path_ = parent / (prefix + std::to_string(::getpid()) + "-" + std::to_string(attempt));
      std::error_code error;
      if (fs::create_directory(path_, error)) return;
      if (error) break;
    }
    path_.clear();
  }
  ~WorkDir() {
    std::error_code ignored;
    if (!path_.empty()) fs::remove_all(path_, ignored);
  }
  WorkDir(const WorkDir&) = delete;
  WorkDir& operator=(const WorkDir&) = delete;
  const fs::path& path() const { return path_; }

private:
  fs::path path_;
};

// Work directories whose process no longer runs: a build killed outright
// (SIGKILL, a crash) leaves its directory behind.
void removeAbandoned(const fs::path& dir, const std::string& prefix) {
  std::error_code error;
  for (fs::directory_iterator it(dir, error), end; !error && it != end; it.increment(error)) {
    const std::string name = it->path().filename().string();
    if (name.rfind(prefix, 0) != 0) continue;
    const long pid = std::strtol(name.c_str() + prefix.size(), nullptr, 10);
    if (pid > 0 && ::kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH) {
      std::error_code ignored;
      fs::remove_all(it->path(), ignored);
    }
  }
}

// Moves a cache entry aside, then removes it. A process that has its module
// loaded keeps running: the file stays mapped.
void discardEntry(const fs::path& root, const fs::path& entry) {
  std::error_code error;
  if (!fs::exists(entry, error)) return;
  for (int attempt = 0; attempt < 1000; ++attempt) {
    const fs::path stale =
        root / (kStalePrefix + std::to_string(::getpid()) + "-" + std::to_string(attempt));
    if (fs::exists(stale, error)) continue;
    fs::rename(entry, stale, error);
    if (!error) fs::remove_all(stale, error);
    return;
  }
}

void interrupted(DesignBuild& result) {
  result.interrupted = true;
  result.error = "interrupted";
}

void build(const DesignRequest& request, const Toolchain& tools, DesignBuild& result) {
  if (request.sources.empty()) {
    result.error = "no Verilog source files given";
    return;
  }
  std::error_code error;
  std::vector<std::string> sources;
  for (const auto& source : request.sources) {
    const fs::path real = fs::canonical(source, error);
    if (error || !fs::is_regular_file(real, error)) {
      result.error = "cannot read source '" + source + "'";
      return;
    }
    sources.push_back(real.string());
  }
  std::vector<std::string> includes;
  for (const auto& dir : request.includeDirs) {
    const fs::path real = fs::canonical(dir, error);
    if (error || !fs::is_directory(real, error)) {
      result.error = "cannot read include directory '" + dir + "'";
      return;
    }
    includes.push_back(real.string());
  }
  const fs::path cwd = fs::canonical(fs::current_path());
  const fs::path root = request.cacheRoot.empty() ? defaultCacheRoot() : request.cacheRoot;
  fs::create_directories(root, error);
  if (!fs::is_directory(root, error)) {
    result.error = "cannot create the design cache '" + root.string() + "'";
    return;
  }
  const fs::path temp = fs::canonical(fs::temp_directory_path());
  removeAbandoned(root, kStagePrefix);
  removeAbandoned(root, kStalePrefix);
  removeAbandoned(temp, kCheckPrefix);
  removeAbandoned(temp, kBuildPrefix);

  // 1. Verilator's lint pass, every time: all its messages for the design,
  //    through the late stages (LATCH, UNOPTFLAT). Paths as the user gave
  //    them, so the messages name files the same way; this process's working
  //    directory is theirs, and Verilator searches it for `include files after
  //    the -I directories.
  std::vector<std::string> design(std::begin(kFlags), std::end(kFlags));
  if (!request.top.empty()) design.insert(design.end(), {"--top-module", request.top});
  for (const auto& dir : request.includeDirs) design.push_back("-I" + dir);
  design.insert(design.end(), request.sources.begin(), request.sources.end());
  WorkDir check(temp, kCheckPrefix);
  if (check.path().empty()) {
    result.error = "cannot create a work directory in '" + temp.string() + "'";
    return;
  }
  std::vector<std::string> command{tools.verilator.string(), "--lint-only"};
  command.insert(command.end(), design.begin(), design.end());
  const ProcessResult lint = runProcess(command, check.path());
  if (cancelRequested()) return interrupted(result);
  if (!lint.started) {
    result.error = "cannot run Verilator (" + tools.verilator.string() + ")";
    return;
  }
  result.diagnostics = lint.errors;
  if (lint.exitCode != 0) {
    result.error = "Verilator rejected the design";
    return;
  }

  // 2. Its front end again, as JSON: the top modules and every file read,
  //    including `include files.
  command = {tools.verilator.string(), "--json-only", "--json-only-output",
             (check.path() / "tree.json").string(), "--prefix", "Vdesign", "-Mdir", check.path().string()};
  command.insert(command.end(), design.begin(), design.end());
  const ProcessResult frontEnd = runProcess(command, check.path());
  if (cancelRequested()) return interrupted(result);
  if (frontEnd.exitCode != 0) {
    result.diagnostics += frontEnd.errors;
    result.error = "Verilator rejected the design";
    return;
  }
  const auto tops = topModules(readText(check.path() / "tree.json"));
  const auto files = filesRead(readText(check.path() / "Vdesign.tree.meta.json"));
  if (!tops || !files) {
    result.error = "cannot read Verilator's description of the design";
    return;
  }
  const TopModule* top = nullptr;
  if (!request.top.empty()) {
    const auto named = std::find_if(tops->begin(), tops->end(),
                                    [&](const TopModule& module) { return module.name == request.top; });
    if (named == tops->end()) {
      result.error = "Verilator did not report the top module '" + request.top + "'";
      return;
    }
    top = &*named;
  } else if (tops->size() == 1) {
    top = &tops->front();
  } else {
    std::string names;
    for (const auto& module : *tops) names += (names.empty() ? "" : ", ") + module.name;
    result.error = tops->empty() ? "the design has no top-level module"
                                 : "the design has several top-level modules (" + names
                                       + "); choose one with --top NAME";
    return;
  }
  result.top = top->name;
  if (request.validate) {
    result.error = request.validate(result.top, top->inputs);
    if (!result.error.empty()) return;
  }
  // Verilator's own standard library files are covered by its version line.
  const std::string verilatorRoot = fs::weakly_canonical(tools.verilatorDir).string() + "/";
  std::vector<fs::path> found;  // every name a design file was found by, absolute
  for (const auto& file : *files) {
    if (file.real.rfind(verilatorRoot, 0) == 0) continue;
    result.files.push_back(file.real);
    if (file.found.empty() || file.found.front() == '<') continue;  // Verilator's own, such as <verilated_std>
    const fs::path name(file.found);
    found.push_back(name.is_absolute() ? name : cwd / name);
  }
  std::sort(result.files.begin(), result.files.end());
  result.files.erase(std::unique(result.files.begin(), result.files.end()), result.files.end());
  // The module is also built from these, keyed below.
  const std::vector<std::string> engineParts{
      "src/engine/SimEngine.h", "src/engine/SimEngine.cpp", "src/engine/VerilatorEngine.h",
      "src/engine/VerilatorEngine.cpp", "src/design/ModuleAbi.h", "src/design/ModuleAbi.cmake",
      "src/design/module/CMakeLists.txt", "src/design/module/factory.cpp"};
  std::vector<fs::path> stamped(result.files.begin(), result.files.end());
  for (const auto& part : engineParts) stamped.push_back(tools.sourceDir / part);
  // Contents between two identical stamps are the contents the stamps
  // describe.
  const std::string checked = stateOf(stamped, found);
  std::map<std::string, std::string> content;
  for (const auto& file : result.files) content[file] = contentOf(file);
  const std::string state = stateOf(stamped, found);
  const bool unreadable = std::any_of(content.begin(), content.end(), [](const auto& entry) { return entry.second.empty(); });
  if (checked.empty() || state.empty() || unreadable) {
    result.error = "cannot read the design's files; were they just moved?";
    return;
  }
  if (state != checked) {
    result.error = "a design file changed while it was being checked; run again";
    return;
  }
  const std::string contents = contentsOf(content);

  // 3. The cache key: everything that shapes the module. Source and include
  //    order matter to Verilator; the working directory does not, beyond the
  //    files it supplied, which are among the files read.
  Fingerprint key;
  key.add("vb-design-cache-2");
  key.add(abiString());
  key.add(lint.output.substr(0, lint.output.find('\n')));  // Verilator's version line
  key.add(tools.verilator.string());
  key.add(tools.verilatorDir.string());
  key.add(tools.cxx.string());
  key.add(result.top);
  for (const auto& dir : includes) key.add(dir);
  key.add("sources");
  for (const auto& source : sources) key.add(source);
  key.add(contents);
  for (const auto& part : engineParts) key.addFile(tools.sourceDir / part);
  const fs::path entry = root / key.hex();
  result.module = entry / "design.so";
  if (request.rebuild) {
    discardEntry(root, entry);
  } else if (fs::is_regular_file(result.module, error)) {
    result.reused = true;
    result.ok = true;
    return;
  } else if (error && error != std::errc::no_such_file_or_directory && error != std::errc::not_a_directory) {
    // Not a missing module (that is a miss) but, say, one we may not read.
    result.error = "cannot read the design cache entry '" + entry.string() + "': " + error.message();
    return;
  }

  // 4. Build it in the temporary directory. CMake's verilate() cannot build
  //    from paths with spaces, semicolons or bytes outside ASCII (Verilator
  //    writes those octal-escaped into the JSON it reads), and symbolic links
  //    alone do not help: Verilator records real paths. A design with such a
  //    path is built from copies of the files Verilator read, in a mirror tree
  //    with plain names (mirrorWay), checked against the contents the key was
  //    made from.
  if (!plainPath(temp.string())) {
    result.error = "cannot build in the temporary directory '" + temp.string()
        + "': CMake needs a path without spaces or special characters; set TMPDIR to another directory";
    return;
  }
  // Where the module is staged: a cache that cannot take it fails now, not
  // after the compile.
  WorkDir stage(root, kStagePrefix);
  if (stage.path().empty()) {
    result.error = "cannot write to the design cache '" + root.string() + "'";
    return;
  }
  const auto plain = [](const std::vector<std::string>& paths) {
    return std::all_of(paths.begin(), paths.end(), [](const std::string& path) { return plainPath(path); });
  };
  const bool copied = !plain(sources) || !plain(includes) || !plainPath(cwd.string()) || !plain(result.files)
      || std::any_of(found.begin(), found.end(), [](const fs::path& name) { return !plainPath(name.string()); });
  // Only an `include by absolute path is read by the build where it is, not
  // from the copies, so its path, and where it leads, must be plain.
  // (Absolute source and -I paths are copied like any other.)
  for (const auto& file : *files) {
    const fs::path name(file.found);
    if (!name.is_absolute() || (plainPath(file.found) && plainPath(file.real))
        || file.real.rfind(verilatorRoot, 0) == 0)
      continue;
    const bool source = std::find(request.sources.begin(), request.sources.end(), file.found) != request.sources.end();
    const bool throughInclude = std::any_of(request.includeDirs.begin(), request.includeDirs.end(),
        [&](const std::string& dir) { return file.found.rfind(dir + "/", 0) == 0; });
    if (source || throughInclude) continue;
    result.error = "'" + file.found + "' is included by an absolute path with, or leading to, spaces, "
        "semicolons or characters outside ASCII, which the build cannot use; include it through an -I "
        "directory instead";
    return;
  }
  if (request.onBuildStart) request.onBuildStart(result.top);
  WorkDir work(temp, kBuildPrefix);
  if (work.path().empty()) {
    result.error = "cannot create a work directory in '" + temp.string() + "'";
    return;
  }
  const fs::path mirror = work.path() / "design";
  const auto place = [&](const std::string& real) { return copied ? mirrored(mirror, real) : fs::path(real); };
  if (copied) {
    for (const auto& name : found) mirrorWay(mirror, name);
    for (const auto& file : result.files) mirrorWay(mirror, file);
    for (const auto& dir : includes) mirrorWay(mirror, dir);
    mirrorWay(mirror, cwd);
    const bool changed = std::any_of(result.files.begin(), result.files.end(),
                                     [&](const std::string& file) { return contentOf(place(file)) != content[file]; });
    if (changed) {
      result.error = "a design file changed during the build, so the module was not stored; run again";
      return;
    }
  }
  std::string sourceList, includeList;
  for (const auto& source : sources) sourceList += (sourceList.empty() ? "" : ";") + place(source).string();
  for (const auto& dir : includes) includeList += place(dir).string() + ";";
  // The build runs elsewhere: the user's working directory, which Verilator
  // searched last, comes last.
  includeList += place(cwd.string()).string();
  const fs::path binary = work.path() / "cmake";
  const fs::path logs = work.path() / "out";
  fs::create_directory(logs);
  const ProcessResult configure = runProcess(
      {tools.cmake.string(), "-S", (tools.sourceDir / "src/design/module").string(), "-B",
       binary.string(), "-Wno-dev", "-DCMAKE_BUILD_TYPE=", "-DCMAKE_CXX_COMPILER=" + tools.cxx.string(),
       "-Dverilator_DIR=" + tools.verilatorDir.string(), "-DVB_SOURCE_DIR=" + tools.sourceDir.string(),
       "-DVB_DESIGN_TOP=" + result.top, "-DVB_DESIGN_SOURCES=" + sourceList,
       "-DVB_DESIGN_INCLUDES=" + includeList},
      logs);
  if (cancelRequested()) return interrupted(result);
  ProcessResult compile;
  if (configure.exitCode == 0) {
    const unsigned jobs = std::max(1u, std::thread::hardware_concurrency());
    compile = runProcess({tools.cmake.string(), "--build", binary.string(), "-j", std::to_string(jobs)},
                         logs);
    if (cancelRequested()) return interrupted(result);
  }
  const fs::path built = binary / "libvb_design.so";
  if (configure.exitCode != 0 || compile.exitCode != 0 || !fs::is_regular_file(built, error)) {
    // Only the failing step: its errors, else all it printed.
    const ProcessResult& failed = configure.exitCode != 0 ? configure : compile;
    result.buildLog = failed.errors.empty() ? failed.output : failed.errors;
    result.error = failed.started ? "the design module failed to build"
                                  : "cannot run CMake (" + tools.cmake.string() + ")";
    return;
  }

  // 5. The module must match its key: the build read only the files the
  //    check read, or their copies (by path, identity and size), and none
  //    changed, not even by an edit that was undone or a link pointed
  //    elsewhere and back. An `include by absolute path, or a header that
  //    appeared in an earlier include directory, is read by the build itself.
  const auto inputs = buildInputs(binary);
  if (!inputs) {
    result.error = "cannot find the list of files the build read (Vdesign__verFiles.dat)";
    return;
  }
  std::map<std::string, std::optional<std::pair<long long, long long>>> known;
  for (const auto& file : result.files) {
    known[file] = identityOf(file);
    if (copied) known[place(file).string()] = identityOf(place(file));
  }
  for (const auto& input : *inputs) {
    // The Verilator program, wherever the installation keeps it: listed by
    // its bare name, or an executable (a header of that name is checked).
    const std::string program = fs::path(input.path).filename().string();
    if ((program == "verilator_bin" || program == "verilator_bin_dbg")
        && (input.path.find('/') == std::string::npos || ::access(input.path.c_str(), X_OK) == 0))
      continue;
    const std::string real = fs::weakly_canonical(input.path).string();
    if (real.rfind(verilatorRoot, 0) == 0) continue;
    const auto expected = known.find(real);
    if (expected == known.end()) {
      result.error = "the build read '" + input.path + "', which the check did not, so the module was not "
          "stored; run again";
      return;
    }
    if (!expected->second || *expected->second != std::make_pair(input.inode, input.size)) {
      result.error = "the build read a different '" + input.path + "' than the check did, so the module "
          "was not stored; run again";
      return;
    }
  }
  const bool rewritten = !copied && std::any_of(result.files.begin(), result.files.end(), [&](const std::string& file) {
    return contentOf(file) != content[file];
  });
  if (stateOf(stamped, found) != state || rewritten) {
    result.error = "a design file changed during the build, so the module was not stored; run again";
    return;
  }

  // 6. Publish the whole entry with one rename, from the cache root's own
  //    file system.
  fs::copy_file(built, stage.path() / "design.so");
  {
    std::ofstream manifest(stage.path() / "manifest.txt");
    manifest << "top " << result.top << '\n';
    for (const auto& file : result.files) manifest << "file " << file << '\n';
  }
  fs::rename(stage.path(), entry, error);
  if (error && !fs::is_regular_file(result.module)) {
    // Not another process's identical module: a damaged entry. Replace it.
    discardEntry(root, entry);
    error.clear();
    fs::rename(stage.path(), entry, error);
  }
  if (!fs::is_regular_file(result.module, error)) {
    result.error = "cannot store the design module in '" + entry.string() + "'";
    return;
  }
  result.ok = true;
}
}  // namespace

Toolchain Toolchain::fromBuild() {
  Toolchain tools;
  const std::string verilatorRoot = environment("VB_VERILATOR_ROOT", "");
  tools.verilator = verilatorRoot.empty() ? fs::path(VB_TOOL_VERILATOR)
                                          : fs::path(verilatorRoot) / "bin" / "verilator";
  tools.verilatorDir = verilatorRoot.empty() ? fs::path(VB_TOOL_VERILATOR_DIR) : fs::path(verilatorRoot);
  tools.cmake = environment("VB_CMAKE", VB_TOOL_CMAKE);
  tools.cxx = VB_TOOL_CXX;
  tools.sourceDir = VB_SOURCE_DIR;
  return tools;
}

fs::path defaultCacheRoot() {
  const std::string override = environment("VB_DESIGN_CACHE", "");
  if (!override.empty()) return override;
  const std::string home = environment("HOME", "/tmp");
  return fs::path(home) / "Library" / "Caches" / "VirtualBasys" / "designs";
}

DesignBuild buildDesign(const DesignRequest& request, const Toolchain& tools) {
  DesignBuild result;
  try {
    build(request, tools, result);
  } catch (const std::exception& failure) {
    // File-system trouble, such as a full disk or a vanished directory.
    result.error = failure.what();
  }
  result.ok = result.ok && result.error.empty();
  return result;
}

}  // namespace vb::design
