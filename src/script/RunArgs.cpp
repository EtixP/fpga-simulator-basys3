#include "script/RunOptions.h"

#include <algorithm>
#include <charconv>

namespace vb {

RunArgs parseRunArgs(int argc, char** argv, std::string defaultXdc) {
  RunArgs out;
  out.xdcPath = std::move(defaultXdc);
  std::vector<std::string> atSpecs;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const bool hasValue = i + 1 < argc;
    if (arg == "--xdc" && hasValue) {
      out.xdcPath = argv[++i];
    } else if (arg == "--frames" && hasValue) {
      const std::string v = argv[++i];
      long frames = 0;
      const auto res = std::from_chars(v.data(), v.data() + v.size(), frames);
      if (res.ec != std::errc{} || res.ptr != v.data() + v.size() || frames < 0)
        out.errors.push_back("--frames '" + v + "': expected a non-negative integer");
      else
        out.run.maxFrames = frames;
    } else if (arg == "--screenshot" && hasValue) {
      out.run.screenshotPath = argv[++i];
    } else if (arg == "--log" && hasValue) {
      out.run.logPath = argv[++i];
    } else if (arg == "--at" && hasValue) {
      atSpecs.push_back(argv[++i]);
    } else if (arg == "--send" && hasValue) {
      const std::string spec = argv[++i];
      const size_t colon = spec.find(':');
      SendEvent e;
      const auto res = colon == std::string::npos
                           ? std::from_chars(spec.data(), spec.data(), e.cycle)
                           : std::from_chars(spec.data(), spec.data() + colon, e.cycle);
      if (colon == std::string::npos || res.ec != std::errc{} ||
          res.ptr != spec.data() + colon || colon + 1 >= spec.size()) {
        out.errors.push_back("--send '" + spec + "': expected CYCLE:TEXT");
      } else {
        e.text = spec.substr(colon + 1);
        out.run.sends.push_back(std::move(e));
      }
    } else if (arg == "--switches" && hasValue) {
      // Rightmost character is SW0; sugar for --at 0:SWn=1. Validate here so
      // errors name --switches, not the expanded --at specs.
      const std::string preset = argv[++i];
      if (preset.size() > 16 ||
          preset.find_first_not_of("01") != std::string::npos) {
        out.errors.push_back("--switches '" + preset +
                             "': expected up to 16 characters of 0/1");
      } else {
        for (size_t c = 0; c < preset.size(); ++c) {
          if (preset[c] != '1') continue;
          const size_t sw = preset.size() - 1 - c;
          atSpecs.push_back("0:SW" + std::to_string(sw) + "=1");
        }
      }
    } else {
      out.errors.push_back("unknown or incomplete argument '" + arg + "'");
    }
  }
  StimulusParse parsed = parseStimulus(atSpecs);
  out.run.stimulus = std::move(parsed.events);
  for (auto& e : parsed.errors) out.errors.push_back(std::move(e));
  std::stable_sort(out.run.sends.begin(), out.run.sends.end(),
                   [](const SendEvent& a, const SendEvent& b) { return a.cycle < b.cycle; });
  return out;
}

}  // namespace vb
