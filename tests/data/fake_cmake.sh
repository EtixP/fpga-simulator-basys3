#!/bin/sh
# Stands in for CMake in the design-loading tests that need a build's cache
# and command-line behaviour, not a compiled module. Configure
# ("-S SRC -B DIR ...") makes DIR; build ("--build DIR ...") writes
# DIR/libvb_design.so, a copy of $VB_FAKE_MODULE, and Verilator's record of
# the files it read, listing no design files. The environment steers it:
#   VB_FAKE_FAIL=configure|build  that step prints to stdout and stderr, then fails
#   VB_FAKE_TOUCH=FILE            the build appends a line to FILE (an edit mid-build)
#   VB_FAKE_UNDO=FILE             the build edits FILE, then puts its contents back
#   VB_FAKE_DEPS=FILE             that record lists FILE as read by the build, with
#                                 its size and inode, or VB_FAKE_DEPS_STAT="SIZE INODE"
#   VB_FAKE_RELINK=LINK           the build points LINK at $VB_FAKE_RELINK_TO, then back
#   VB_FAKE_ARGS=FILE             every call appends its arguments, one per line,
#                                 and the CXXFLAGS and MACOSX_DEPLOYMENT_TARGET it was given
step=configure
dir=
previous=
for argument in "$@"; do
  if [ "$previous" = "-B" ]; then dir=$argument; fi
  if [ "$previous" = "--build" ]; then step=build; dir=$argument; fi
  previous=$argument
done
if [ -n "$VB_FAKE_ARGS" ]; then
  printf '%s\n' "$@" "CXXFLAGS=${CXXFLAGS-unset}" \
    "MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET-unset}" >> "$VB_FAKE_ARGS"
fi
if [ "$VB_FAKE_FAIL" = "$step" ]; then
  echo "fake $step progress"
  echo "fake $step failure" >&2
  exit 1
fi
mkdir -p "$dir" || exit 1
if [ "$step" = build ]; then
  if [ -n "$VB_FAKE_TOUCH" ]; then echo "// edited during the build" >> "$VB_FAKE_TOUCH"; fi
  if [ -n "$VB_FAKE_UNDO" ]; then
    cp "$VB_FAKE_UNDO" "$dir/undo.orig"
    echo "// edited during the build" >> "$VB_FAKE_UNDO"
    cat "$dir/undo.orig" > "$VB_FAKE_UNDO"
  fi
  deps="$dir/CMakeFiles/vb_design.dir/Vdesign.dir"
  mkdir -p "$deps" || exit 1
  echo 'S         0        0           0           0           1           0 "unhashed" "verilator_bin"' \
    > "$deps/Vdesign__verFiles.dat"
  if [ -n "$VB_FAKE_DEPS" ]; then
    identity=${VB_FAKE_DEPS_STAT:-$(stat -f '%z %i' "$VB_FAKE_DEPS")}
    echo "S $identity 3 4 5 6 \"hash\" \"$VB_FAKE_DEPS\"" >> "$deps/Vdesign__verFiles.dat"
  fi
  if [ -n "$VB_FAKE_RELINK" ]; then
    original=$(readlink "$VB_FAKE_RELINK")
    ln -sfn "$VB_FAKE_RELINK_TO" "$VB_FAKE_RELINK"
    ln -sfn "$original" "$VB_FAKE_RELINK"
  fi
  cp "$VB_FAKE_MODULE" "$dir/libvb_design.so" || exit 1
fi
