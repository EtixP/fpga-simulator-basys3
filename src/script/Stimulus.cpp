#include "script/Stimulus.h"

#include "board/BoardModel.h"

#include <algorithm>
#include <charconv>
#include <optional>

namespace vb {

namespace {

// "SW7" -> 7; nullopt if not a switch name.
std::optional<uint32_t> switchIndex(const std::string& name) {
  if (name.size() < 3 || name.compare(0, 2, "SW") != 0) return std::nullopt;
  uint32_t idx = 0;
  const char* b = name.data() + 2;
  const char* e = name.data() + name.size();
  const auto res = std::from_chars(b, e, idx);
  if (res.ec != std::errc{} || res.ptr != e || idx >= BoardModel::kSwitchCount)
    return std::nullopt;
  return idx;
}

std::optional<Button> buttonId(const std::string& name) {
  for (uint32_t i = 0; i < kButtonNames.size(); ++i)
    if (name == kButtonNames[i]) return static_cast<Button>(i);
  return std::nullopt;
}

}  // namespace

StimulusParse parseStimulus(const std::vector<std::string>& specs) {
  StimulusParse out;
  for (const std::string& spec : specs) {
    const size_t colon = spec.find(':');
    const size_t eq = spec.find('=', colon == std::string::npos ? 0 : colon + 1);
    if (colon == std::string::npos || eq == std::string::npos || eq < colon) {
      out.errors.push_back("--at '" + spec + "': expected CYCLE:NAME=V");
      continue;
    }
    StimulusEvent e;
    const char* b = spec.data();
    const auto res = std::from_chars(b, b + colon, e.cycle);
    if (res.ec != std::errc{} || res.ptr != b + colon) {
      out.errors.push_back("--at '" + spec + "': bad cycle number");
      continue;
    }
    e.name = spec.substr(colon + 1, eq - colon - 1);
    const std::string val = spec.substr(eq + 1);
    if (val != "0" && val != "1") {
      out.errors.push_back("--at '" + spec + "': value must be 0 or 1");
      continue;
    }
    e.value = val == "1";
    if (!switchIndex(e.name) && !buttonId(e.name)) {
      out.errors.push_back("--at '" + spec + "': unknown input '" + e.name +
                           "' (SW0..SW15, BTNC/BTNU/BTNL/BTNR/BTND)");
      continue;
    }
    out.events.push_back(std::move(e));
  }
  std::stable_sort(out.events.begin(), out.events.end(),
                   [](const StimulusEvent& a, const StimulusEvent& b) {
                     return a.cycle < b.cycle;
                   });
  return out;
}

bool applyStimulus(BoardModel& board, const StimulusEvent& e) {
  if (const auto sw = switchIndex(e.name)) {
    if (!board.hasSwitch(*sw)) return false;
    board.setSwitch(*sw, e.value);
    return true;
  }
  if (const auto b = buttonId(e.name)) {
    if (!board.hasButton(*b)) return false;
    board.setButton(*b, e.value);
    return true;
  }
  return false;
}

}  // namespace vb
