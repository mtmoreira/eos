# Provenance: abc

Upstream: ABC (Berkeley logic synthesis and verification),
https://github.com/berkeley-abc/abc.
Pinned version or commit: commit
416199292fbda674af618e012e6e60dbf6ad122c (2026-10-17, "Merge pull
request #577"), pinned 2026-10-18 by the Phase 0 licence audit
(upstream publishes no release tags).
Licence: BSD-style (UC Regents custom permissive; the upstream
copyright.txt is kept at this module's root).
Imported: 2026-10-25 (Phase 2 unit 2.6), copied from a pristine
checkout of the pinned commit kept under ~/workspace
(abc-pin-p2). No upstream file was modified during the import:
every imported file is byte-identical to the pristine checkout,
verified file by file by the import script.

## Inventory (2346 imported ABC files)

The import takes the whole tracked upstream src/ tree, with the
upstream layout preserved under this module's src/:

- src/: 2346 files, every file git tracks under the upstream
  src/ directory at the pin (2347 files), minus the one exclusion
  below. This is wider than the compiled closure on purpose: some
  compiled sources include headers from directories the upstream
  Makefile does not compile (src/base/abci/abc.c includes
  map/fpga/fpga.h; src/base/abc/abcBlifMv.c includes
  bdd/extrab/extraBdd.h), so the full tree is imported and the
  build selects what it compiles.
- copyright.txt: the upstream licence text, at the module root.
- arch_flags.c: an upstream build tool, kept at the module root
  and run at CMake configure time to derive the architecture
  defines, exactly as the upstream Makefile runs it. It is not
  compiled into the library.

## The compiled closure (1389 sources)

The CMake source list is generated from the upstream build
definition, not chosen by hand: the Makefile's MODULES list (112
modules) and each module's module.make SRC list give 1390
sources; minus the excluded main.c, 1389 compile into
eda_synth_abc: 1265 C and 124 C++. C sources compile as C; C++
sources compile with the upstream flags -std=c++17
-fno-exceptions. The build defines ABC_USE_CUDD=1 (upstream's
default: ABC's own bundled CUDD under src/bdd) and
ABC_USE_PTHREADS, and does not define ABC_USE_READLINE: the
facade never starts the interactive shell. The library links
Threads, the dynamic loader library and m. The architecture
defines come from arch_flags.c (on the build machine used for
this import: LIN64, SIZEOF_VOID_P=8, SIZEOF_LONG=8,
SIZEOF_INT=4).

## Excluded from the import (with reasons)

- src/base/main/main.c: the standalone abc executable's main().
  Upstream's own library target (libabc.a) filters main.o out of
  the library as well; EOS uses the library and its own facade.
- Everything outside the upstream src/ tree: the upstream
  Makefile and CMake files, test collateral, demo files and
  documentation. The build definition was read to generate the
  compiled closure above; the EOS build replaces the upstream
  build system.

## Local patches

Patch count: 0. No upstream source file is modified.

Build-level accommodations, recorded here instead of as patches:

- The module CMakeLists.txt is EOS's own (as for every adopted
  module): it replaces the upstream build files, which assume
  the standalone abc executable, its install rules and its
  interactive shell.
- Symbol namespacing of the bundled CUDD copy, applied to the
  built archive, never to a source: ABC's bundled CUDD and the
  CUDD 3.0.0 vendored inside the sta module are the same
  library, so a binary linking both engines (the session flow
  links sta for timing and abc for resynthesis) collides on
  their global symbols. The collision was measured, not
  anticipated: 198 duplicate definitions reported by the linker
  from the first three pulled CUDD objects. Dropping CUDD from
  the ABC build is not a way out at this pin (upstream's
  ABC_USE_NO_CUDD configuration does not link:
  proof/cec/cecSatG2.c calls CUDD with no ABC_USE_CUDD guard).
  A post-build step therefore renames every global symbol the
  bundled CUDD objects define with an eosabc_ prefix inside
  libeda_synth_abc.a, definitions and references alike
  (namespace_cudd.py, EOS-authored, at this module's root; it
  works on extracted copies and rebuilds the archive). ABC
  keeps its full upstream capability in process, the sta copy
  keeps its own names, and EOS code outside this module never
  references CUDD symbols, so nothing else needs the prefixed
  names.

- A check_layers.py resolution fix, in tools/ rather than in this
  module: the import made two of odb's system includes falsely
  resolve into this module (the bare "zlib.h" to the bundled
  src/misc/zlib/zlib.h, and "boost/random.hpp" by basename to
  src/sat/cadical/random.hpp), reporting upward dependencies that
  no compiler could produce. Resolution now matches compiler
  reachability: a path-ful include matches only at its path, and a
  bare name reaches only another module's include/ or internal/
  surface, never its src/ tree. Regression tests
  test_path_include_does_not_match_unrelated_basename and
  test_bundled_src_header_does_not_shadow_system_include cover
  both cases (checker pytest 11 passed, was 9).

## The facade (EOS-authored)

EOS exposes only its own facade: include/eos/synth/abc.h and
src/abc_facade.cc. The facade embeds ABC as a library in
process, through upstream's own embedding entry points
(Abc_Start, Abc_FrameGetGlobalFrame, Abc_Stop,
Cmd_CommandExecute); no external process is involved at
runtime. ABC keeps a single global frame, so the facade guards
it: a second live engine fails with kInvalidState rather than
sharing the frame silently.

Two upstream behaviours shaped the facade and are recorded here
because they are load-bearing:

- The equivalence proof (ABC's cec) reads its conclusion from
  what the prover prints: the cec command returns success
  whether or not the networks proved equivalent. The facade
  captures the prover's output and classifies it: "Networks are
  equivalent" is a pass, "Networks are NOT EQUIVALENT" is a
  failure, and anything else (a limit hit, a name mismatch, an
  unreadable netlist) is kUnknown. An unknown verdict is a
  first-class outcome and is never a pass.
- cec's own file reader cannot read a mapped Verilog netlist
  (it reads with the mapped flag off, and the Verilog reader
  refuses a gate netlist once a library is loaded), so each
  netlist is first read as mapped, strashed and written as an
  AIGER intermediate, whose symbol table carries the input and
  output names cec matches on, and cec runs on the AIGER pair.
  ABC's command interpreter also continues a semicolon script
  after a failed step and reports the last step's status, so
  each conversion starts from a restarted frame and the proof
  checks that the AIGER file was actually written: a failed
  read leaves an empty network, produces no AIGER, and
  classifies as kUnknown instead of silently comparing a stale
  network against itself.

Upstream tests are not wired into ctest: upstream's checks are
scripts around the standalone executable, which is excluded
above. The EOS facade is covered instead by EOS tests with
small synthetic fixtures (tests/unit/synth/abc): delay mapping
that removes a double inversion, a proof that accepts the
mapped cone, a proof that rejects a deliberately corrupted
cone, and a proof that cannot run classifying as unknown.

EOS-authored files in this module (not upstream, not patches):
CMakeLists.txt, include/eos/synth/abc.h, src/abc_facade.cc,
namespace_cudd.py (the build tool described above), README.md
and this file.
