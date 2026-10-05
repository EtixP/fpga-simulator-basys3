#pragma once
#include "engine/SimEngine.h"

#include <filesystem>
#include <memory>
#include <string>

namespace vb::design {

// A loaded design module. It stays loaded for the rest of the process: the
// Verilator runtime inside it keeps process-wide state, and engines it made
// may outlive any owner.
class DesignModule {
public:
  // Null, with `error` set, if the file cannot be loaded, is not a design
  // module, or was built for a different engine interface or compiler.
  static std::unique_ptr<DesignModule> load(const std::filesystem::path& path, std::string& error);

  // A new engine for `top`, clocked by the input port `clock`.
  std::unique_ptr<SimEngine> createEngine(const std::string& top, const std::string& clock,
                                          std::string& error) const;

private:
  explicit DesignModule(void* create) : create_(create) {}
  void* create_;  // CreateFunction
};

}  // namespace vb::design
