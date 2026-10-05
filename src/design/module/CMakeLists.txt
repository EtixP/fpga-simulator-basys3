# A design module: one user design, verilated and compiled into a loadable
# library with the project's own engine code. Configured by the app at run
# time (src/design/DesignBuilder.cpp), never part of the main build.
#
# Inputs: VB_SOURCE_DIR, VB_DESIGN_TOP, VB_DESIGN_SOURCES and
# VB_DESIGN_INCLUDES (;-lists of absolute paths without spaces; the app
# passes other paths through symbolic links). The last include directory is
# the user's working directory, which Verilator searched during the check.
cmake_minimum_required(VERSION 3.24)
project(vb_design_module CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(verilator REQUIRED)
include("${VB_SOURCE_DIR}/src/design/ModuleAbi.cmake")

add_library(vb_design MODULE
  factory.cpp
  "${VB_SOURCE_DIR}/src/engine/SimEngine.cpp"
  "${VB_SOURCE_DIR}/src/engine/VerilatorEngine.cpp"
)
target_include_directories(vb_design PRIVATE "${VB_SOURCE_DIR}/src")
target_compile_definitions(vb_design PRIVATE VB_ENGINE_INTERFACE_HASH="${VB_ENGINE_INTERFACE_HASH}")
# The engine runs once per simulated cycle, as in the prebuilt launchers.
target_compile_options(vb_design PRIVATE -O2)

# The examples' flags, except -Wall (shipped examples only) and with warnings
# non-fatal: user designs run with their warnings shown (R5). Every source is
# Verilog-2005, as in the app's check (DesignBuilder.cpp kFlags).
verilate(vb_design
  SOURCES ${VB_DESIGN_SOURCES}
  TOP_MODULE ${VB_DESIGN_TOP}
  PREFIX Vdesign
  INCLUDE_DIRS ${VB_DESIGN_INCLUDES}
  OPT_FAST "-O2" OPT_GLOBAL "-O2"
  VERILATOR_ARGS --public-flat-rw --trace-vcd --timescale 1ns/1ns -Wno-fatal
                 --default-language 1364-2005
)
