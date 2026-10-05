# Sets VB_ENGINE_INTERFACE_HASH from the headers the app and a design module
# share. Included by the app's build and by every module build, so both sides
# compute it the same way. Requires VB_SOURCE_DIR.
file(SHA256 "${VB_SOURCE_DIR}/src/engine/SimEngine.h" _vb_engine_header_hash)
file(SHA256 "${VB_SOURCE_DIR}/src/design/ModuleAbi.h" _vb_module_abi_hash)
string(SHA256 VB_ENGINE_INTERFACE_HASH "${_vb_engine_header_hash}${_vb_module_abi_hash}")
# Recompute when either header changes.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${VB_SOURCE_DIR}/src/engine/SimEngine.h" "${VB_SOURCE_DIR}/src/design/ModuleAbi.h")
