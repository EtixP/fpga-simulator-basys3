// The two C entry points of a design module (see design/ModuleAbi.h). The
// generated model class is always Vdesign (verilated with --prefix Vdesign).
#include "Vdesign.h"
#include "design/ModuleAbi.h"
#include "engine/VerilatorEngine.h"

#include <cstring>
#include <exception>

extern "C" const char* vb_design_abi() {
  static const std::string abi = vb::design::abiString();
  return abi.c_str();
}

extern "C" vb::SimEngine* vb_create_engine(const char* top, const char* clock, char* error,
                                           std::size_t errorSize) {
  try {
    return vb::makeVerilatorEngine<Vdesign>({.topModule = top, .clockName = clock}).release();
  } catch (const std::exception& failure) {
    if (error && errorSize) {
      std::strncpy(error, failure.what(), errorSize - 1);
      error[errorSize - 1] = '\0';
    }
    return nullptr;
  }
}
