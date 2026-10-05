#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vb::design {

// Just what the loader needs from Verilator's --json-only dumps, with a small
// JSON reader (no dependency). nullopt when the text is not the expected JSON.
// Verilator writes bytes outside ASCII in file names as octal escapes
// ("\352\263\274"), which the reader accepts.

struct TopModule {
  std::string name;
  std::vector<std::string> inputs;  // its input ports, in declaration order
};

// The top-level modules (hierarchy level 1) in <prefix>.tree.json, in source
// order.
std::optional<std::vector<TopModule>> topModules(std::string_view treeJson);

// A file Verilator read: its name as found (as given, or an -I directory
// joined with the `include text; relative to the working directory) and its
// real path.
struct FileRead {
  std::string found;
  std::string real;
};

// Every file Verilator read, from <prefix>.tree.meta.json, in the order
// listed. Pseudo-files such as "<built-in>" are left out. One real file can
// appear under several names.
std::optional<std::vector<FileRead>> filesRead(std::string_view metaJson);

}  // namespace vb::design
