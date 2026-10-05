#include "design/DesignModule.h"

#include "design/ModuleAbi.h"

#include <dlfcn.h>

namespace vb::design {

std::unique_ptr<DesignModule> DesignModule::load(const std::filesystem::path& path,
                                                 std::string& error) {
  // RTLD_LOCAL: the module's Verilator runtime stays private to it. Never
  // closed (see the class comment).
  void* handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    const char* reason = ::dlerror();
    error = "cannot load the design module: " + std::string(reason ? reason : path.string());
    return nullptr;
  }
  const auto abi = reinterpret_cast<AbiFunction>(::dlsym(handle, kAbiSymbol));
  void* create = ::dlsym(handle, kCreateSymbol);
  if (!abi || !create) {
    error = "'" + path.string() + "' is not a VirtualBasys design module";
    return nullptr;
  }
  if (abi() != abiString()) {
    error = "the design module was built for a different VirtualBasys build; rebuild "
            "VirtualBasys or delete the design cache";
    return nullptr;
  }
  return std::unique_ptr<DesignModule>(new DesignModule(create));
}

std::unique_ptr<SimEngine> DesignModule::createEngine(const std::string& top,
                                                      const std::string& clock,
                                                      std::string& error) const {
  char message[1024] = {};
  SimEngine* engine =
      reinterpret_cast<CreateFunction>(create_)(top.c_str(), clock.c_str(), message, sizeof message);
  if (!engine) error = message[0] ? message : "the design module could not create an engine";
  return std::unique_ptr<SimEngine>(engine);
}

}  // namespace vb::design
