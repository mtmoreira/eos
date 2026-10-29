#!/bin/bash
# setup-macos.sh: install the EOS build prerequisites on macOS.
#
# Idempotent: safe to re-run. Everything comes from Homebrew except
# the objcopy symlink described below. This script installs build
# prerequisites only; it does not configure or build EOS.
#
# Why each piece is needed:
#   tcl-tk  - the EOS shell needs Tcl headers. Without them the build
#             still succeeds, but eda reports "this build has no Tcl:
#             the EOS shell is unavailable".
#   cmake   - the build system.
#   llvm    - the ABC build step calls objcopy to namespace its
#             bundled CUDD symbols. macOS object files are Mach-O,
#             which GNU objcopy (Homebrew binutils) cannot read at
#             all: it fails on the first object. LLVM's objcopy
#             understands Mach-O, so this script links it as
#             objcopy on PATH.
#   python3 - the ABC build step runs a Python script.
#   bison, flex, swig, eigen, spdlog, fmt, zlib - required by the
#             imported OpenDB and OpenSTA modules (parsers, code
#             generation, linear algebra, compression, logging).
#             fmt is explicit, not just a spdlog dependency: when
#             the compiler lacks std::format, OpenSTA needs fmt
#             headers, and Homebrew's spdlog ships no bundled copy.
# The same set is what cmake/EosDependencies.cmake checks at
# configure time; this script is what -DEOS_AUTO_INSTALL_DEPS=ON
# runs on macOS.
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "setup-macos.sh: this script is for macOS only (uname: $(uname -s))" >&2
  exit 1
fi

if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew is required but not installed." >&2
  echo "Install it from https://brew.sh, then re-run this script." >&2
  exit 1
fi

if ! xcode-select -p >/dev/null 2>&1; then
  echo "Xcode Command Line Tools are missing; starting the install..."
  xcode-select --install
  echo "Re-run this script after the Command Line Tools install finishes." >&2
  exit 1
fi

brew install tcl-tk cmake llvm python3 bison flex swig eigen spdlog fmt zlib

# objcopy under its expected name, resolving to LLVM's objcopy.
# Unconditional and idempotent: it also repairs an earlier link to
# GNU objcopy, which cannot read Mach-O objects. Never links
# anything else as objcopy.
LLVM_OBJCOPY="$(brew --prefix llvm)/bin/llvm-objcopy"
if [[ -x "$LLVM_OBJCOPY" ]]; then
  ln -sf "$LLVM_OBJCOPY" "$(brew --prefix)/bin/objcopy"
  echo "Linked objcopy -> $LLVM_OBJCOPY"
else
  echo "setup-macos.sh: llvm-objcopy not found after brew install llvm" >&2
  exit 1
fi

echo "Verifying..."
command -v cmake
command -v objcopy
objcopy --version | head -1
# Homebrew's tcl-tk formula configures Tcl with
# --includedir=<prefix>/include/tcl-tk, so the headers live under
# include/tcl-tk/, not include/. Accept both layouts; plain
# include/ is what a non-Homebrew Tcl install uses.
TCLTK_PREFIX="$(brew --prefix tcl-tk)"
if [[ -f "$TCLTK_PREFIX/include/tcl-tk/tcl.h" ]]; then
  echo "tcl.h found under $TCLTK_PREFIX/include/tcl-tk"
elif [[ -f "$TCLTK_PREFIX/include/tcl.h" ]]; then
  echo "tcl.h found under $TCLTK_PREFIX/include"
else
  echo "setup-macos.sh: tcl.h not found under $TCLTK_PREFIX/include/tcl-tk or $TCLTK_PREFIX/include" >&2
  exit 1
fi

echo
echo "Prerequisites installed. Build with:"
echo "  cmake -S . -B build -DEOS_BUILD_TESTS=OFF"
echo "  cmake --build build --target eda -j"
echo "Then run ./build/apps/eda/eda for the interactive shell."
echo "(cmake/EosDependencies.cmake adds the Homebrew prefixes at"
echo "configure time, so no CMAKE_PREFIX_PATH flag is needed.)"
