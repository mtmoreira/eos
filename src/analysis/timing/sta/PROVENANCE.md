# Provenance: sta

Upstream: OpenSTA.
Pinned version or commit: commit
0f2529d18218b87aa0e82de444395a830d5058a1 (2026-10-16, "Sta update
1015", project version 3.1.0), pinned 2026-10-18 by the Phase 0
licence audit.
Licence: GPL-3.0 (the upstream LICENSE file is kept in this
directory; the distribution decision stays deferred per Phase 0 and
the module boundary is the containment).
Imported: 2026-10-25 (Phase 2 unit 2.2), copied from a pristine
checkout of the pinned commit kept under ~/workspace
(opensta-pin-p2). No upstream file was modified during the import.

## Inventory (404 imported OpenSTA files, plus the CUDD closure)

The import keeps the upstream layout, rooted at this module:

- include/sta/: 112 public headers (upstream include/sta)
- src/app/: 2 (StaMain.cc and StaApp.i; Main.cc excluded, below)
- src/dcalc/: 38, src/graph/: 4, src/liberty/: 37, src/network/: 14,
  src/parasitics/: 20, src/power/: 16, src/sdc/: 24, src/sdf/: 12,
  src/search/: 72, src/spice/: 8, src/tcl/: 11, src/util/: 24,
  src/verilog/: 8 (the upstream source directories, top-level files:
  .cc, .hh, .ll, .yy, .i and .tcl sources, plus
  util/StaConfig.hh.cmake and dcalc/Arnoldi.txt)
- src/etc/: 1 (TclEncode.tcl, the build tool that encodes the Tcl
  init sources into a generated C++ file at build time)
- LICENSE: 1 (upstream LICENSE, GPL-3.0)

src/dcalc/DcalcAnalysisPt.cc is imported with its directory but,
exactly as upstream at this pin, is not in the build's source list.

## The CUDD dependency closure (87 files, src/cudd/)

At this pin, search/Bdd.cc, power/Power.cc, search/Sim.cc and
spice/WriteSpice.cc include cudd.h unconditionally, so a build with
no CUDD at all would require patching upstream sources. The unit
therefore carries no external CUDD dependency; instead CUDD 3.0.0,
the exact version upstream's own Dockerfile.ubuntu24.04 builds, is
vendored as a dependency closure inside this module, the same
pattern unit 2.1 used for the utl Logger inside odb. Source:
https://raw.githubusercontent.com/davidkebo/cudd/main/cudd_versions/cudd-3.0.0.tar.gz
Licence: BSD-style (the CUDD LICENSE file is kept at src/cudd/LICENSE).

- src/cudd/cudd/: 63 (the 61 cudd/*.c sources that make up
  libcudd, cudd.h, cuddInt.h; the tarball's cudd/ directory also
  holds CUDD's own test programs testcudd.c and testextra.c, which
  are not part of libcudd and are not imported)
- src/cudd/util/: 13 (the 11 util sources libcudd contains:
  cpu_stats, cpu_time, cstringstream, datalimit, pathsearch,
  pipefork, prtime, safe_mem, strsav, texpand, ucbqsort, plus
  util.h and cstringstream.h, the latter included because
  cstringstream.c includes it)
- src/cudd/st/: 2 (st.c, st.h), src/cudd/epd/: 3 (epd.c, epd.h and
  epdInt.h), src/cudd/mtr/: 4 (mtrBasic.c, mtrGroup.c, mtr.h and
  mtrInt.h); the two Int headers are the internal headers the
  vendored sources include, part of the same tarball
- src/cudd/LICENSE: 1
- src/cudd/config.h: 1, the configuration header produced by
  running CUDD's own ./configure for this platform in a scratch
  extract outside the repo. It is a generated build configuration
  file, not a modification of any CUDD source, and is the one
  vendored CUDD file that is not byte-identical to the tarball.

The file set matches the object list of a reference libcudd.a built
from the same tarball. The CUDD sources compile as C objects folded
into eda_analysis_sta. The rest of the CUDD distribution (dddmp,
nanotrav, the C++ wrapper, tests and the autotools build system) is
not part of libcudd and is not imported.

## The Tcl and SWIG layer (difference from odb)

Upstream odb needed no bindings in EOS because its operations are
plain C++ calls. OpenSTA is different: SDC reading and several load
operations exist upstream only as Tcl commands, so the Tcl layer is
part of the engine itself. This import therefore includes the
upstream Tcl sources (encoded into the binary at build time by
src/etc/TclEncode.tcl, as upstream does) and the SWIG wrapper
generated from src/app/StaApp.i with the upstream options
(-module sta -namespace -prefix sta). The wrapper is folded into
eda_analysis_sta as objects. EOS still exposes only its own facade:
the facade owns a Tcl interpreter, registers the wrapper with it,
loads files through the upstream commands in process, and answers
slack queries through the upstream C++ API. No external process is
involved at runtime.

## Excluded from the import (with reasons)

- app/Main.cc: the standalone executable entry point. EOS uses the
  library and its own facade; StaMain.cc (init helpers) is imported.
- test/ (the upstream regression suite, about 251M of data) and the
  test/ subdirectory inside each source directory: regression data
  and Tcl harness files, not part of the C++ library.
- examples/, doc/: example designs and documentation, not library
  sources.
- cmake/, and etc/ other than TclEncode.tcl (Build.sh,
  CheckCmdHelp.tcl, CodeCoverage.sh, FindMessages.tcl,
  WriteCmdDocs.tcl, build_sta.sh): upstream build and maintenance
  helpers the EOS build does not use.
- Upstream build system and project files: CMakeLists.txt (root and
  per-directory test files), BUILD.bazel, Jenkinsfile,
  Dockerfile.ubuntu22.04, Dockerfile.ubuntu24.04,
  Dockerfile.ubuntu26.04, Brewfile, mkdocs.yml, README.md and the
  .git* files: replaced by the EOS build and this module's own
  README.md.

## Local patches

Patch count: 0. No upstream source file is modified, and no CUDD
source file is modified.

Build-level accommodations, recorded here instead of as patches:

- The module CMakeLists.txt is EOS's own (as for every adopted
  module): it replaces the upstream build files, which assume the
  standalone OpenSTA project (its own sta executable, install rules
  and regression harness).
- C++20 is set on this module's targets only: OpenSTA at this pin
  requires C++20 (upstream sets cxx_std_20), while EOS is C++17
  elsewhere.
- Parsers are generated with flex and bison from the six upstream
  grammar pairs (Verilog, Liberty, LibExpr, Sdf, Spef, Saif), the
  Tcl init sources are encoded with TclEncode.tcl, and StaConfig.hh
  is generated from src/util/StaConfig.hh.cmake with STA_VERSION
  3.1.0 and STA_GIT_SHA1 set to the pinned commit, all exactly as
  upstream builds them.
- std::format: the build performs upstream's check for <format>
  with -std=c++20; where std::format is unavailable it falls back
  to the bundled fmt inside spdlog, as upstream does.
- The upstream warning set is not applied: EOS does not build with
  -Werror and the imported code is not warning-clean under that set
  with this compiler. The SWIG wrapper keeps upstream's own warning
  suppressions. Warnings do not fail this build.
- System packages the build needs beyond the previous CI set: flex
  and swig (generation), and libeigen3-dev (Eigen headers, a
  header-only dependency upstream requires). The CI build job
  installs them explicitly. Tcl, bison, zlib and spdlog were
  already in the CI set.

Upstream tests are not wired into ctest: the regression suite
needs its Tcl harness and about 251M of data files, which are
excluded above. The EOS facade is covered instead by EOS tests with
a small synthetic fixture (tests/unit/analysis/sta), including a
precision test whose expected worst slack was produced by the
pinned upstream OpenSTA executable built locally from the same
commit on the identical fixture (see the test source for the
numbers and their provenance).

EOS-authored files in this module (not upstream, not patches):
CMakeLists.txt, include/eos/analysis/sta.h, src/sta_facade.cc,
src/cudd/config.h (generated, see above), README.md and this file.
