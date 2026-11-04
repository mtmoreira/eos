# fin: port adaptations (EOS-owned changes)

The pin tree is never modified. In this unit, NO imported file is
patched at all: every file listed in PROVENANCE.md is a byte-identical
copy of the upstream file at ead3236 (verified by SHA-256, see
PROVENANCE.md). Every adaptation below lives only in EOS-owned files
(CMakeLists.txt, README.md, PROVENANCE.md, this file) or in the
finish runner that consumes this module (src/pd/finish). This is
stricter than the grt precedent (which patched a formatter into a
port copy): the fin sources compile against the in-tree headers
without any source change, so no patch is needed and none is made.

## 1. No Tcl/SWIG factory (EOS-owned construction)

Upstream's MakeFinale.cpp / MakeFinale.h, finale.i, finale-py.i,
and finale.tcl are not imported. EOS constructs fin::Finale
directly (Finale::init, then densityFill) in the finish runner
(src/pd/finish/src/finish.cc) and registers eos::finish through
its own registry, not through engine Tcl bindings. The only
consequence is that sta/StaMain.hh (included only by the
unimported MakeFinale.cpp) never enters the build.

## 2. Message codes (EOS-owned Logger, no codegen)

Upstream's messages.txt is not imported. The in-tree Logger
already carries the FIN tool id (see
src/db/odb/include/utl/Logger.h), and DensityFill.cpp's
logger->info/warn/error calls use it directly. No message
codegen runs at build time.

## 3. GUI surface (dbsta stub, debug-gated upstream)

graphics.cpp uses gui::Gui and gui::Painter, resolved by the
dbsta module's gui stub (the same stub that covers grt's
FastRouteRenderer). DensityFill constructs its Graphics object
only when debug is set AND the GUI is active
(DensityFill.cpp: `if (debug && Graphics::guiActive())`), and
the finish runner never enables debug, so no GUI object is ever
constructed in a headless run. The renderer code still links
through the stub, as in grt.

## 4. Include layout (CMakeLists.txt, EOS-owned glue)

Upstream fin translation units include their sibling headers bare
(DensityFill.h, graphics.h, polygon.h), resolved upstream by
src/fin/src. The port keeps those headers in src/, so the fin
CMakeLists adds src/ as a PRIVATE include directory (the same
pattern stt/grt use). No imported source file is edited to add
a prefix, and the public header path (fin/Finale.h) is unchanged
for consumers.

## 5. Boost headers (declared system dependency, never vendored)

DensityFill.h includes boost/property_tree and polygon.h
includes boost/polygon; graphics.cpp uses boost/lexical_cast.
These are header-only Boost uses, covered by the already-declared
Boost headers dependency (see third_party_manifest.yaml and
cmake/EosDependencies.cmake, which require libboost-dev; CI
already installs it). No Boost source is copied into this module,
and no new system dependency is introduced: the P5 drt import
already requires Boost, and the top-level dependency check
already requires Boost headers for the in-tree OpenDB polygon code.
