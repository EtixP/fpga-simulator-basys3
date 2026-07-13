cmake_minimum_required(VERSION 3.24)
project(VirtualBasys CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Homebrew Verilator ships verilator-config.cmake; provides verilate() and
# VERILATOR_ROOT. Versions pinned in docs/versions.md.
find_package(verilator REQUIRED)

enable_testing()
add_subdirectory(src)
add_subdirectory(tests)
