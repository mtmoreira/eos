# pdn: port adaptations (EOS-owned changes)

The pin tree is never modified. In this unit, NO imported file is
patched at all: every file listed in PROVENANCE.md is a byte-identical
copy of the upstream file at ead3236 (verified by SHA-256, see
PROVENANCE.md). Every adaptation below lives only in EOS-owned files
(CMakeLists.txt, README.md, PROVENANCE.md, this file) or in the
finish runner that consumes this module (src/pd/finish). The pdn
sources compile against the in-tree headers without any source
change, so no patch is needed and none is made.

## 1. No Tcl/SWIG factory (EOS-owned construction)

Upstream's MakePdnGen.cc / MakePdnGen.hh, PdnGen.i,
PdnGen-py.i, and pdn.tcl are not imported. EOS constructs
pdn::PdnGen directly (PdnGen::init, setCoreDomain, makeCoreGrid,
makeFollowpin, makeStrap, makeConnect, buildGrids, writeToDb)
in the finish runner (src/pd/finish/src/finish.cc) and registers
eos::finish through its own registry, not through engine Tcl
bindings.

## 2. Message codes (EOS-owned Logger, no codegen)

Upstream's messages.txt is not imported. The in-tree Logger
already carries the PDN tool id (see
src/db/odb/include/utl/Logger.h), and the pdn sources' logger
calls use it directly. No message codegen runs at build time.

## 3. GUI surface (dbsta stub, debug-gated upstream)

renderer.cpp uses gui::Gui and gui::Painter, resolved by the
dbsta module's gui stub (the same stub that covers grt's
FastRouteRenderer). The PDNRenderer object is constructed only
by PdnGen::setDebugRenderer, and the finish runner never enables
it, so no GUI object is ever constructed in a headless run. The
renderer code still links through the stub, as in grt.

## 4. ord include (vestigial upstream, no shim use)

PdnGen.cc includes ord/OpenRoad.hh but never calls ord:: in any
pdn source (verified by grep at import time). The include
resolves through the dbsta ord shim, and no shim behaviour is
relied on: the runner passes the database and logger to
PdnGen::init directly, exactly as the extract runner passes them
to rcx::Ext.

## 5. Include layout (CMakeLists.txt, EOS-owned glue)

Upstream pdn translation units include their sibling headers bare
(connect.h, domain.h, grid.h, ...), resolved upstream by
src/pdn/src. The port keeps those headers in src/, so the pdn
CMakeLists adds src/ as a PRIVATE include directory (the same
pattern stt/grt use). No imported source file is edited to add
a prefix, and the public header path (pdn/PdnGen.hh) is unchanged
for consumers.

## 6. Boost headers (declared system dependency, never vendored)

shape.h includes boost/geometry and boost/geometry/index/rtree.
These are header-only Boost uses, covered by the already-declared
Boost headers dependency (see third_party_manifest.yaml and
cmake/EosDependencies.cmake, which require libboost-dev; CI
already installs it). No Boost source is copied into this module,
and no new system dependency is introduced: the P5 drt import
already requires Boost, and the top-level dependency check
already requires Boost headers for the in-tree OpenDB polygon code.
