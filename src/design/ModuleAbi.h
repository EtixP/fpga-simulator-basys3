#pragma once
// The contract between the app and a design module that it built at run time
// (src/design/module). Both compile this header. A module whose ABI string
// differs from the app's is never used: the two would disagree about the
// SimEngine interface they pass across.
#include "engine/SimEngine.h"

#include <cstddef>
#include <string>

// VB_ENGINE_INTERFACE_HASH: SHA-256 of engine/SimEngine.h and this header,
// computed by CMake (src/design/ModuleAbi.cmake) for the app and every module.
#ifndef VB_ENGINE_INTERFACE_HASH
#error "VB_ENGINE_INTERFACE_HASH must be defined (see src/design/ModuleAbi.cmake)"
#endif

namespace vb::design {

inline constexpr const char* kAbiSymbol = "vb_design_abi";
inline constexpr const char* kCreateSymbol = "vb_create_engine";

// Exported by every module with C linkage.
using AbiFunction = const char* (*)();
// Returns a new engine owned by the caller, or null with a message in `error`.
using CreateFunction = SimEngine* (*)(const char* top, const char* clock, char* error,
                                      std::size_t errorSize);

// The interface hash plus the C++ library ABI both sides pass std::string
// and the SimEngine vtable across. Not the exact compiler version: after a
// compiler update, modules built by the new compiler still load in an app
// built by the old one.
inline std::string abiString() {
  std::string abi = "vb-design-2;" VB_ENGINE_INTERFACE_HASH;
#ifdef _LIBCPP_ABI_VERSION
  abi += ";libc++ abi " + std::to_string(_LIBCPP_ABI_VERSION);
#else
  abi += ";unknown C++ library";
#endif
  return abi;
}

}  // namespace vb::design
