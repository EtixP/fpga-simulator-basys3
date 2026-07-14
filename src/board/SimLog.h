#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace vb {

// The structured simulation log (R3): cycle-stamped peripheral events.
// Collection is OPT-IN — an interactive GUI session on a busy design would
// otherwise accumulate tens of thousands of lines per second unboundedly;
// headless/golden tests and `--log` runs enable it explicitly.
//
// Timestamp semantics (stated in the header lines, frozen byte-for-byte —
// goldens depend on them):
//  - outputs are observed on an absolute grid (every kSampleChunkCycles of
//    now()): "[cycle N] X a->b" means the change was observed at N and
//    occurred within the preceding chunk;
//  - input events are exact-cycle and take effect at edge N+1;
//  - per grid crossing the emission order is canonical: SSEG digits 0..3
//    (content line before .dp line), then LEDs ascending. Input lines sit
//    between crossings in poke order, on actual transitions only; a poke at
//    exactly a crossing cycle is stamped equal to and ordered AFTER that
//    crossing's output lines (the driver ticks to the crossing, then pokes).
class SimLog {
public:
  // chunkCycles appears in the header so a log is self-describing.
  explicit SimLog(uint64_t chunkCycles);

  void setEnabled(bool on) { enabled_ = on; }
  bool enabled() const { return enabled_; }

  // Appends "[cycle N] <body>". No-op when disabled.
  void event(uint64_t cycle, const std::string& body);

  // Header + events, in emission order.
  const std::vector<std::string>& lines() const { return lines_; }
  void clear();  // resets to just the header

private:
  uint64_t chunkCycles_;
  bool enabled_ = false;
  std::vector<std::string> lines_;
};

}  // namespace vb
