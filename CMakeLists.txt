cmake_minimum_required(VERSION 3.24)
project(VirtualBasys CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Homebrew Verilator ships verilator-config.cmake; provides verilate() and
# VERILATOR_ROOT. Versions pinned in docs/versions.md.
find_package(verilator REQUIRED)

option(VB_BUILD_GUI "Build the Dear ImGui SDL2/Metal GUI (macOS)" ON)

enable_testing()
add_subdirectory(examples)  # verilated design libraries (sim_*)
add_subdirectory(src)       # engine, constraints, board, gui
add_subdirectory(tests)
