#include "board/SimLog.h"

namespace vb {

SimLog::SimLog(uint64_t chunkCycles) : chunkCycles_(chunkCycles) { clear(); }

void SimLog::event(uint64_t cycle, const std::string& body) {
  if (!enabled_) return;
  lines_.push_back("[cycle " + std::to_string(cycle) + "] " + body);
}

void SimLog::clear() {
  lines_.clear();
  // FROZEN header — golden logs compare byte-for-byte against these lines.
  // Wording changes are golden churn; bump v= instead if semantics change.
  lines_.push_back("# virtualbasys-log v=1 chunk_cycles=" + std::to_string(chunkCycles_) +
                   " grid=absolute");
  lines_.push_back(
      "# outputs: observed at grid crossings; \"[cycle N] X a->b\" = changed within "
      "the preceding chunk");
  lines_.push_back(
      "# inputs: exact-cycle, transition-only; a poke at cycle N is first sampled at "
      "edge N+1");
  lines_.push_back(
      "# order per crossing: SSEG[0..3] (content then .dp), LED ascending; SSEG "
      "content is character-level");
  lines_.push_back(
      "# this is an observation record, not an exhaustive event record: output "
      "activity shorter than one chunk is not visible");
}

}  // namespace vb
