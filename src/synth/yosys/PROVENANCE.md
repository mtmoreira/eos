# Provenance: yosys

Upstream: Yosys (Yosys Open SYnthesis Suite),
https://github.com/YosysHQ/yosys.
Pinned version or commit: tag v0.70, commit
28ba3cb926fe376fa05ebfc1b921747d011d4557 (2026-10-13, "Release
version 0.70"), pinned 2026-10-18 by the Phase 0 licence audit.
Licence: ISC (the upstream COPYING is kept at this module's root).
Imported: 2026-10-29 (synthesis unit S0), copied from a pristine
clone of the pinned tag kept under ~/workspace (yosys-0.70-pin).
No upstream file was modified during the import: every imported
file is byte-identical to the pristine checkout, verified file by
file by SHA-256 in the import script.

## Inventory (845 imported files, plus COPYING)

The import takes the source tree, with the upstream layout
preserved under this module's src/:

- kernel/: 75 files, the RTLIL core, pass registry, logging and
  the Verilog-independent machinery, including the build inputs
  kernel/cellhelp.py, kernel/version.cc.in and
  kernel/yosys_config.h.in.
- frontends/: 30 files, every frontend except slang (see
  exclusions): verilog (the front end this phase uses), ast,
  rtlil, json, blif, aiger, liberty, rpc and the verific stub.
- passes/: 245 files, every pass family: cmds, equiv, fsm,
  hierarchy, memory, opt, pmgen, proc, sat, techmap and tests.
- backends/: 50 files, every writer backend.
- techlibs/: 317 files, every vendor techlib including its .v
  cell data, except the functional-test data directories (see
  exclusions).
- libs/: 128 files across the libraries the compiled closure
  links: bigint (15), ezsat (8), flex (3, the FlexLexer header
  the Verilog lexer includes), fst (16), json11 (3), minisat
  (30), sha1 (3), subcircuit (8) and symfpu (40, headers and
  sources for the smt2/symfpu pass), plus libs/cxxopts (2: the
  header-only options library's include/cxxopts.hpp and LICENSE,
  used only by the excluded driver unit).
- COPYING: the upstream licence text, at the module root.

## The compiled closure (336 upstream TUs plus generated files)

The CMake source list is the upstream CMake build's compile
closure at the S0 configuration (no ABC, no slang, no Python, no
Verific library), taken from that build's compile_commands.json:
336 upstream translation units compile into eda_synth_yosys, plus
the EOS facade, plus three generated units: kernel/version.cc
(from version.cc.in, with the pin's version and commit baked in),
the flex Verilog lexer and the bison Verilog parser. Generated
headers (yosys_config.h, the cellhelp .inc files, the pmgen
*_pm.h headers) are produced at build time into the binary dir
at the same relative paths the sources include them by, so no
source is edited to find them.

## Excluded from the import (with reasons)

- abc/ (the YosysHQ abc submodule): EOS already has its own ABC
  (src/synth/abc). Yosys's abc pass shells out to an abc
  executable, which the EOS no-external-process rule forbids;
  the EOS ABC facade is the ABC path for this phase.
- frontends/slang/ and libs/slang, libs/tomlplusplus,
  libs/boost_regex, frontends/slang/lib (the sv-elab
  submodule): the slang frontend and its libraries, disabled at
  this pin. The front-end decision is plan Section 6, open
  decision 1; the Phase 0 pin is the Verilog front end.
- docs/, examples/, tests/, guidelines/, misc/, nix/, pyosys/,
  .github and the root build files (Makefile, CMakeLists.txt,
  Brewfile, flake files): documentation, examples, functional
  tests around the yosys executable, packaging and the upstream
  build system. The EOS build replaces the upstream build files,
  as for ABC. Upstream's functional tests are not wired into
  ctest: they drive the standalone executable, which is excluded
  below. The EOS facade is covered instead by EOS tests with a
  small synthetic fixture (tests/unit/synth/yosys).
- techlibs/*/tests/: functional-test data for the vendor
  techlibs, not part of the library.
- libs/fmt (the fmt submodule, 12.2): not imported because EOS
  already depends on system fmt, and no compiled Yosys TU
  outside the excluded driver unit includes fmtlib headers.
- libs/dlfcn-win32: Windows-only.
- libs demo/test/sample mains (bigint sample.cc and
  testsuite.cc, ezsat demo_*.cc, puzzle3d.cc and testbench.cc,
  subcircuit demo.cc and scshell.cc): standalone programs, not
  library code, excluded on the same rule as ABC's main.c.
- Upstream per-directory CMakeLists.txt and Makefiles: build
  definitions, replaced by this module's CMakeLists.txt.

## Local patches

Patch count: 0. No upstream source file is modified.

Build-level accommodations, recorded here instead of as patches:

- The module CMakeLists.txt is EOS's own (as for every adopted
  module): it replaces the upstream build files and the upstream
  component system, with the same source list and the same
  generated-file steps (flex/bison, cellhelp, pmgen, version,
  config).
- C++20 for this target only: Yosys requires C++20 at this pin
  while the EOS top level is C++17. The facade header exposes no
  Yosys types, so no other EOS target needs C++20.
- Feature configuration, chosen to match the EOS CI environment:
  ABC, slang, readline, editline and libffi off (CI installs
  none of their development packages; upstream auto-disables
  readline, editline and libffi when absent); zlib and threads
  on; Tcl on when the EOS Tcl dependency is found (CI installs
  tcl-dev), off otherwise, with tclapi.cc compiled either way
  under its own YOSYS_ENABLE_TCL guard, exactly as upstream.
- Two library exclusions, both main-bearing units imported but
  not compiled into the library: kernel/driver.cc (the yosys
  executable's main() and its console log sink, referenced by
  nothing else in the tree) and passes/techmap/filterlib.cc
  (the standalone yosys-filterlib tool: the file is just
  FILTERLIB plus a textual #include of libparse.cc, so its
  object defines main() and a FILTERLIB-mode LibertyParser).
  The library instead compiles libparse.cc itself: without
  FILTERLIB its main() is not compiled in, and it provides the
  LibertyParser the dfflibmap, clockgate and libcache passes
  use. Getting this pair backwards has a visible failure mode,
  found while testing this unit: without libparse.o, the linker
  satisfied the LibertyParser symbols from filterlib.o, whose
  main() then shadowed the test binary's main and the binary
  ran as the filterlib tool instead of the test suite.
- The share directory is not wired in this unit: passes that
  need techlib .v data at runtime (the synth_* passes) look it
  up relative to the host executable, which is not an EOS
  concept. The front end, which this unit exercises, needs no
  share data. Wiring share data to an EOS-owned location is S1
  work, when elaborate first runs a pass that needs it.
- Whole-archive linking is mandatory for every consumer of
  eda_synth_yosys: Yosys registers its passes and frontends
  through global constructors, and a plain static-archive pull
  never includes an object nothing references, so read_verilog
  (and every other pass) would be unregistered at runtime and
  the call would fail with "No such command". Upstream's
  shared-library default hides this; its static users link
  whole-archive, and EOS consumers do the same, with CMake's
  LINK_LIBRARY:WHOLE_ARCHIVE feature (needs CMake 3.24 or
  later; CI and the local toolchain both exceed it). The unit
  test links this way, and S1's consumer must too.
- The gatemate and quicklogic generated data files
  (lut_tree_cells.genlib, lut_tree_map.v, bram_types_sim.v) are
  runtime share data, not compile inputs, so the EOS build does
  not generate them in this unit; they arrive with share wiring.

## Submodule pins inside the import

The v0.70 tree references submodules; only the two the compiled
closure uses are imported, at the commits the superproject
records at the tag: libs/cxxopts at
4bf61f08697b110d9e3991864650a405b3dd515d (v3.2.1) and
libs/symfpu at 5d5e50867437fe5cabd6ab5aad3b3808ec86701f
(branch floatWithStatusFlags). The fmt submodule at the tag is
a00f82c6b66dcae15503ba19c1598b2733057dea (12.2.0-33-ga00f82c6);
it is not imported, for the reason above.

## The facade (EOS-authored)

EOS exposes only its own facade: include/eos/synth/yosys.h and
src/yosys_facade.cc. The facade drives Yosys in process through
upstream's own embedding entry points (yosys_setup, run_pass);
no external process is involved at runtime. Yosys keeps a single
global design, so the facade guards it: a second live engine
fails with kInvalidState rather than sharing the design
silently. Each ParseVerilog call replaces the global design
object with a fresh one, so a previous parse cannot leak modules
into the next one, and the call runs under Yosys's command-error
throw scope, so a command-level rejection comes back as a Status.

Two session facts shaped the facade and are recorded here
because a natural implementation gets both wrong:

- Pass registration is one-shot per process. Pass objects
  register through global constructors into a queue that
  yosys_setup drains, and yosys_shutdown clears the registries
  without refilling the queue, so a second setup registers
  nothing and every later command fails with "No such command".
  The facade therefore sets Yosys up exactly once per process
  and never shuts it down; an engine's destructor releases only
  the single-engine guard. (This was found by testing: the
  first facade version shut the session down per parse, and
  every parse after the first failed exactly this way.)
- The log sinks are installed by the yosys executable's main,
  which this library does not compile, so a library session
  starts with no sinks and all Yosys output is discarded. The
  facade installs upstream's non-tty default sink
  (ConsoleLogSink) at first setup.

Two upstream behaviours shaped the facade and are recorded here
because they are load-bearing:

- A Yosys log_error (a syntax error in the input, an
  unsupported construct in a pass) terminates the process with
  _Exit: upstream's behaviour for its executable, with no
  library hook that turns it into a return value. ParseVerilog
  therefore fails fast only on an unreadable file (kIoError,
  before Yosys is touched). A malformed file is a documented
  limitation of this unit, not a tested failure mode: testing it
  would kill the test runner. Unit S1 must resolve this before
  read_verilog changes semantics to parse.
- Yosys's pass output goes to stderr as upstream prints it; the
  facade does not redirect it in this unit.

EOS-authored files in this module (not upstream, not patches):
CMakeLists.txt, include/eos/synth/yosys.h, src/yosys_facade.cc,
README.md and this file.
