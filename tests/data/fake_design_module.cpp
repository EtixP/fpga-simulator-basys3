// A loadable library that claims to be a design module built for another
// VirtualBasys: the loader must refuse it before calling into it.
#include <cstddef>

extern "C" const char* vb_design_abi() { return "vb-design-0;some other build"; }

extern "C" void* vb_create_engine(const char*, const char*, char*, std::size_t) { return nullptr; }
