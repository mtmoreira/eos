# Provenance: odb

Upstream: OpenDB, from the OpenROAD repository (src/odb), plus the
Logger and its companion utilities from the same repository's
src/utl.
Pinned version or commit: OpenROAD commit
ead3236e56d57362a8dc8b25658fb41a191eba17, the OpenROAD pin of
ORFS v3.0 and the writer of the ORFS v3.0 gcd 6_final.odb binary
the previous pin could not read (docs/odb-repin-decision.md, unit
F6; re-pin executed by fix track unit REPIN, authorized
2026-10-28). The previous pin was OpenROAD v2.0, commit
a5a56584fed1523f384e2944ee8439256ab5e12d (2021-07-16), pinned
2026-10-18 by the Phase 0 licence audit and imported 2026-10-25
(Phase 2 unit 2.1); its inventory remains in git history, and this
re-pin replaces the module tree wholesale.
Licence: BSD-3-Clause (the upstream LICENSE file is kept in this
directory).
Imported: 2026-10-28 (fix track unit REPIN), copied from the ORFS
v3.0 OpenROAD tree kept under ~/workspace (openroad-ead3236).
Every imported file below was verified byte-identical to its
upstream source at import time, except src/lef/README, which
upstream ships as a symlink to lefdefReadme.txt and is imported
here as a regular file with that target's content. No upstream
file was modified during the import.

## Inventory (584 imported files: 583 under src/ and include/, plus LICENSE)

At the pin, OpenDB lives in the OpenROAD tree at src/odb, and utl
is the separate OpenROAD module src/utl. The import keeps that
layout, rooted at this module:

- include/odb/: 42 public headers (upstream include/odb,
  including version.hh.in, which no imported source includes; the
  generated version.hh is not needed)
- include/utl/: 9 headers (upstream src/utl/include/utl)
- src/db/: 257 files, the database core (upstream src/odb/src/db,
  including odb.tcl, which is not built)
- src/defin/: 43, src/defout/: 4, DEF reader and writer
- src/lefin/: 28, src/lefout/: 2, LEF reader and writer
- src/def/: 78 (the DEF parser library: def/ and defzlib/, plus
  its CMakeLists.txt, LICENSE.TXT, README.md and lefdefReadme.txt)
- src/lef/: 56 (the LEF parser library: lef/ and lefzlib/, plus
  its CMakeLists.txt, LICENSE.TXT, README.md, lefdefReadme.txt
  and the dereferenced README)
- src/zutil/: 4, src/cdl/: 2 (CDL writer)
- src/utl/: 8 (Logger.cpp, LoggerCommon.cpp, LoggerCommon.h,
  MakeLogger.cpp, Metrics.cpp, CFileUtils.cpp,
  ScopedTemporaryFile.cpp, timer.cpp; upstream src/utl/src)
- src/codeGenerator/: 50 (the schema and generator that produced
  the generated files committed inside src/db; kept for provenance,
  not built: the generated sources are committed upstream and are
  what this module compiles)
- LICENSE: 1 (upstream OpenDB LICENSE)

The upstream per-directory CMakeLists.txt files inside src/ are kept
unmodified for provenance. They are not part of the EOS build; the
module's own CMakeLists.txt builds the whole closure as the single
target eda_db_odb and generates the two bison parsers (def, lef)
from the upstream grammars, as upstream does. Built sources: 247
upstream .cpp files, plus the EOS facade and the 2 bison parsers.

## Excluded from the import (with reasons)

- test/ (162 regular files, plus symlinks: Tcl regression tests
  and C++ tests), doc/,
  Dockerfile, README.md of upstream OpenDB, and messages.txt:
  not part of the C++ library. The upstream C++ tests in test/cpp do
  not run standalone against the imported library without the
  upstream Tcl test harness and its data files, so they are not
  wired into ctest; the EOS facade is covered instead by EOS tests
  with small synthetic LEF/DEF fixtures (tests/unit/db/odb), plus
  one environment-gated check on the real ORFS v3.0 gcd database
  (see below).
- src/swig/ (20 files): the Tcl and Python bindings. EOS exposes the
  database through its own facade and registry, never through
  upstream bindings. The two utl SWIG interface files (Logger.i,
  Logger-py.i) are excluded on the same rule.
- src/def and src/lef command-line tools (defrw, defwrite, defdiff,
  lefrw, lefwrite, lefdiff) and the cdef and clef C API variants
  with their zlib variants (103 files), their TEST data (4 files)
  and their doc/ directories (6 files): executables and C API
  variants the C++ library does not link. The parser libraries
  themselves (def, defzlib, lef, lefzlib) are imported in full.

## Facade adaptation at this pin (EOS-authored, not patches)

The facade (src/odb_facade.cc, the only EOS-authored translation
unit in the target) was adapted to the ead3236 API, after a syntax
probe of the adapted facade against the candidate headers closed
the break list below with no further breaks:

- dbDatabase::read and write take std::istream& and std::ostream&
  instead of FILE*; ReadDb and WriteDb now use file streams.
- lefin::createTechAndLib takes a technology name first
  (tech_name, lib_name, lef_file); the facade passes "eos_tech".
- lefin::createLib takes the technology first (dbTech*, name,
  lef_file); the facade holds the dbTech the first LEF read
  created (from the new library's getTech, falling back to the
  database's getTech) and passes it on later LEF reads.
- defin::createChip takes a mandatory dbTech* third argument; the
  facade passes the held technology.
- dbITerm::connect and disconnect are member functions now
  (term->connect(net), term->disconnect()), not static functions.
- Public headers move from include/opendb to include/odb; the
  facade's includes follow. The facade's public surface
  (include/eos/db/odb.h) is unchanged.

## Checkpoint format consequence

EOS's own WriteDb checkpoint files move from schema minor 51 (the
v2.0 reader) to minor 74 at this pin (the candidate reader's minor
is 74 with initial 57). Checkpoints written by the old pin do not
survive the re-pin. Checkpoints are runtime artifacts, not
committed fixtures; nothing in the tree stores one. The reverse
direction is the point of the re-pin: EOS now reads ORFS v3.0
outputs directly, which the odb unit test suite checks on the real
ORFS v3.0 sky130hd gcd 6_final.odb when EOS_ORFS_ODB_PATH points
at it (block gcd, 1046 instances, 394 nets, plus a WriteDb /
ReadDb round trip through the new pin); the check skips in CI,
where the evidence file is absent.

## Local patches

Patch count: 0. No upstream source file is modified.

Build-level accommodations, recorded here instead of as patches:

- The module CMakeLists.txt is EOS's own (as for every adopted
  module): it replaces the upstream build files, which depend on the
  OpenROAD superbuild (its messages() macro, SWIG and Tcl targets).
  Upstream's db target links Tcl for its bindings; EOS builds no
  bindings, and no imported source includes a Tcl header, so the
  target links Tcl only when CMake finds it, as before.
- The target compiles with -include cstdint, kept from the v2.0
  import at build level; the ead3236 headers include <cstdint>
  themselves, so it is belt and braces only.
- src/fmt_compat.h, the EOS-authored fmt shim force-included for
  the v2.0 import, is deleted in this unit: the ead3236 sources
  target the modern fmt API and the target builds clean against
  the system fmt without it.
- System packages the build needs beyond the previous CI set: bison
  (parser generation), zlib (system library, as upstream links it)
  and spdlog (the utl Logger is built on spdlog, with its fmt
  dependency); boost headers are used by the LEF property parsers in
  src/lefin. The CI build job installs them explicitly.

EOS-authored files in this module (not upstream, not patches):
CMakeLists.txt, include/eos/db/odb.h, src/odb_facade.cc,
README.md and this file.
