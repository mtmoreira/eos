# Provenance: odb

Upstream: OpenDB, from the OpenROAD repository (src/OpenDB), plus the
Logger from the same repository's src/utl.
Pinned version or commit: OpenROAD v2.0, commit
a5a56584fed1523f384e2944ee8439256ab5e12d (2021-07-16), pinned
2026-10-18 by the Phase 0 licence audit.
Licence: BSD-3-Clause (the upstream LICENSE file is kept in this
directory).
Imported: 2026-10-25 (Phase 2 unit 2.1), copied from a pristine
checkout of the pinned commit kept under ~/workspace
(openroad-pin-p2). No upstream file was modified during the import.

## Inventory (525 imported files)

At the pin, OpenDB lives in the OpenROAD tree at src/OpenDB (the
src/odb path in the EOS plan and in later upstream releases). The
import keeps that layout, rooted at this module:

- include/opendb/: 60 public headers (upstream include/opendb)
- include/utl/: 3 headers (upstream src/utl/include/utl)
- src/db/: 226 files, the database core (upstream src/OpenDB/src/db)
- src/defin/: 39, src/defout/: 4, DEF reader and writer
- src/lefin/: 18, src/lefout/: 2, LEF reader and writer
- src/def/: 76 (the DEF parser library: def/ and defzlib/, plus its
  CMakeLists.txt and LICENSE.TXT)
- src/lef/: 52 (the LEF parser library: lef/ and lefzlib/, plus its
  CMakeLists.txt)
- src/zutil/: 7, src/zlib/: 2 (ZException only; compression itself
  uses the system zlib, as upstream's parser libraries do),
  src/tm/: 4 (the Z* object factory), src/cdl/: 2 (CDL writer)
- src/utl/: 2 (Logger.cpp and MakeLogger.cpp, upstream src/utl/src)
- src/codeGenerator/: 27 (the schema and generator that produced the
  generated files committed inside src/db; kept for provenance, not
  built: the generated sources are committed upstream and are what
  this module compiles)
- LICENSE: 1 (upstream OpenDB LICENSE)

The upstream per-directory CMakeLists.txt files inside src/ are kept
unmodified for provenance. They are not part of the EOS build; the
module's own CMakeLists.txt builds the whole closure as the single
target eda_db_odb and generates the two bison parsers (def, lef)
from the upstream grammars, as upstream does.

## Excluded from the import (with reasons)

- test/ (143 files: Tcl regression tests and C++ tests), doc/,
  jenkins/, Dockerfile, Jenkinsfile, README.md of upstream OpenDB:
  not part of the C++ library. The upstream C++ tests in test/cpp do
  not run standalone against the imported library without the
  upstream Tcl test harness and its data files, so they are not
  wired into ctest; the EOS facade is covered instead by EOS tests
  with small synthetic LEF/DEF fixtures (tests/unit/db/odb).
- src/swig/ (19 files): the Tcl and Python bindings. EOS exposes the
  database through its own facade and registry, never through
  upstream bindings.
- src/def and src/lef command-line tools (defrw, defwrite, defdiff,
  the cdef C variants and their test data): executables and C API
  variants the C++ library does not link. The parser libraries
  themselves (def, defzlib, lef, lefzlib) are imported in full.

## Local patches

Patch count: 0. No upstream source file is modified.

Build-level accommodations, recorded here instead of as patches:

- The module CMakeLists.txt is EOS's own (as for every adopted
  module): it replaces the upstream build files, which depend on the
  OpenROAD superbuild (its messages() macro, SWIG and Tcl targets).
- The target compiles with -include cstdint: the 2021 headers use
  uint32_t and uint64_t without including <cstdint>, which the
  libstdc++ of their era pulled in transitively and GCC 13 does not.
- src/fmt_compat.h (EOS-authored) is force-included into the target
  the same way: it restores the pre-fmt9 enum formatting (enums as
  their underlying integer) that the upstream utl::Logger calls rely
  on, which fmt 9 and later refuse without an explicit formatter.
  It modifies no upstream file.
- System packages the build needs beyond the previous CI set: bison
  (parser generation), zlib (system library, as upstream links it)
  and spdlog (the utl Logger is built on spdlog, with its fmt
  dependency); boost headers are used by the LEF property parsers in
  src/lefin. The CI build job installs them explicitly.

EOS-authored files in this module (not upstream, not patches):
CMakeLists.txt, include/eos/db/odb.h, src/odb_facade.cc,
src/fmt_compat.h, README.md and this file.
