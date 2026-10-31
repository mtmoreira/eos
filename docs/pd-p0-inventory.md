# Place-and-route unit P0: pin and import inventory

Date: 2026-10-31. Phase: place-and-route, unit P0. This document is
the P0 deliverable: the compile verification of the OpenDB pin
commit's place-and-route engine directories against the in-tree
OpenDB and OpenSTA headers, the licence audit for those
directories, and the dependency inventory for the stage units P1
to P7. It is an inventory, not an import: no engine code is
imported by P0, no stage command exists yet, and structure.yaml is
unchanged. Imports land per stage unit, each with its own
PROVENANCE.md inventory. The headline result is mixed and is
stated as measured: the odb surface is fully compatible, the
licence audit is clean, and the remaining gaps (sta headers and
signature, fmt version, OR-Tools, LEMON/C++20, support-module
closure) are each bounded, assigned to a stage unit, and given a
fix direction in Sections 5 and 6.

Judging rules, stated before the results they judge (queue rule):

- A probe PASS means one engine translation unit compiles
  (-fsyntax-only) against the in-tree OpenDB headers and the in-tree
  OpenSTA headers. It is a header/API compatibility verdict, not a
  link verdict: linking needs the support closure in Section 5,
  which is not in tree yet.
- A probe FAIL on a missing support-module header (dbSta, stt, ant,
  dst, upf, rsz, gui, ord) is a closure blocker, not an OpenDB
  incompatibility, and is reported as such. Support headers were
  supplied from the pin tree as stand-ins, so a FAIL naming one
  would mean the stand-in itself is missing, not an incompatibility.
- A probe FAIL on a missing in-tree odb header would be an OpenDB
  incompatibility and would stop the phase under the version-skew
  rule. No such FAIL occurred: the odb surface is fully compatible.
- A probe FAIL on a missing in-tree sta header, or on a changed
  in-tree sta signature, is a measured STA-version gap (Section 5b),
  distinct from odb incompatibility. In-tree sta is OpenSTA commit
  0f2529d; the pin vendors OpenSTA 7a184d00. Five sta headers the
  engines need do not exist in-tree, and one in-tree sta function
  changed signature since the pin. The gap is reported with its
  exact TU impact, not waved through, and its resolution is
  stage-unit glue work (shims) with a re-pin fallback that would
  need the owner's decision (Section 5b).
- A probe FAIL on a fmt static assertion is a measured fmt-version
  gap (Section 5c), distinct from both: drt and FastRoute format
  scoped enums through Logger::report, which system fmt 9.1
  rejects. Logger.h itself is byte-identical between in-tree and
  pin, so the version of fmt underneath is the only variable.
- A FAIL on a missing system package (OR-Tools) is a dependency
  finding, recorded in Section 6 with its install route and its
  first-need unit.
- Licence verdicts cover the compiled library sources only
  (Section 4). Test, doc, bench, calibration, and example files are
  not compiled into EOS and are excluded from the verdict, except
  where a bundled library ships inside a compiled source tree
  (FastRoute inside grt), which is audited as code.

## 1. The pin

All nine engine directories come from OpenROAD commit
ead3236e56d57362a8dc8b25658fb41a191eba17, the same commit that
supplies the in-tree OpenDB (REPIN 2026-10-28,
src/db/odb/PROVENANCE.md). The evaluation tree is the workspace
copy at ~/workspace/openroad-ead3236 (ORFS v3.0 pin, per the
Phase 0 build recipe). Spot verification that the evaluation tree
is the pin the in-tree OpenDB came from: SHA-256 of
include/odb/db.h, src/db/dbDatabase.cpp, and src/lefin/lefin.cpp is
identical between the in-tree module and the evaluation tree.
No other commit is substituted anywhere in this document.

## 2. Per-engine inventory

Counts are compiled library sources: C++ files under each engine's
src/ (and its public include/ headers), excluding test/, doc/,
bench/, calibration, and example files. Make* factory translation
units are Tcl-binding glue (they call Ifp_Init-style registration
through the upstream Tcl interpreter); EOS excludes them from
library imports and provides its own factories, the same way the
synthesis import excluded the Yosys driver main. They are counted
separately below and are not in the probe denominators. The one
exception is grt's MakeWireParasitics.cpp, which is library code
despite its name (see below).

| Engine | Stage | Lib TUs | Public headers | Licence file | Probe PASS |
|---|---|---|---|---|---|
| ifp (InitFloorplan) | P1 floorplan | 1 | 2 | header text only | 0/1 |
| gpl (RePlAce) | P2 placement | 14 | 2 | LICENSE (BSD-3) | 11/14 |
| dpl (OpenDP) | P2 placement | 8 | 2 | LICENSE (BSD-3) | 8/8 |
| cts (TritonCTS) | P3 CTS | 9 | 2 | LICENSE (BSD-3) | 0/9 |
| grt (FastRoute global) | P4 global route | 8 + 8 FastRoute | 4 | LICENSE (BSD-3) | 13/16 |
| drt (TritonRoute) | P5 detailed route | 70 | 2 | LICENSE (BSD-3) | 9/70 |
| rcx (OpenRCX) | P6 extraction | 17 | 6 | header text only | 17/17 |
| pdn (PdnGen) | P7 finish | 14 | 2 | header text only | 14/14 |
| fin (Finale) | P7 finish | 3 | 2 | LICENSE (BSD-3) | 3/3 |
| Total | | 152 | | | 75/152 |

Binding glue excluded from the denominators (9 TUs, one per engine):
ifp MakeInitFloorplan, gpl MakeReplace, dpl MakeOpendp, cts
MakeTritoncts, grt MakeGlobalRouter, drt MakeTritonRoute, rcx
MakeOpenRCX, pdn MakePdnGen, fin MakeFinale. grt's
MakeWireParasitics.cpp is library code, not glue, and IS counted
(it fails on sta/Set.hh like GlobalRouter.cpp). Graphics TUs (gpl
graphics.cpp, dpl Graphics.cpp, cts CtsGraphics.cpp, grt
GrouteRenderer.cpp and heatMap.cpp, drt FlexDR/FlexPA/FlexTA
graphics, pdn renderer.cpp, fin graphics.cpp) are library code
and are counted: they use the gui stub API (gui::Painter), not Qt,
and compile without Qt.

Notes per engine:

- ifp is 1 TU: the floorplan algorithm lives in InitFloorplan.cc
  alone. It needs dbSta and upf (Section 5a) and sta/Vector.hh
  (Section 5b).
- gpl compiles CPU-only: gpuSolver.cu is excluded and the CUDA
  paths in solver.cpp/solver.h/initialPlace.cpp are guarded by
  ENABLE_GPU, which EOS does not define. OR-Tools is used by
  exactly one TU, mbff.cpp, which replace.cpp calls through
  Replace::runMBFF; it cannot be excluded without breaking the
  Replace link (Section 6).
- dpl's library target is dpl_lib (7 TUs: Opendp, dbToOpendp,
  Grid, CheckPlacement, Place, FillerPlacement, OptMirror) plus
  Graphics.cpp: 8 probe TUs, all passing.
- cts's library target is cts_lib (8 TUs) plus CtsGraphics.cpp:
  9 probe TUs. LEMON is used by exactly one TU, Clustering.cpp,
  which HTreeBuilder and SinkClustering call, so LEMON is a
  genuine P3 dependency (with the C++20 interaction in
  Section 6).
- grt's library target is grt_lib (6 TUs: Grid, Net, Pin,
  MakeWireParasitics, RepairAntennas, GlobalRouter) plus heatMap
  and GrouteRenderer: 8 probe TUs, plus the 8 FastRoute TUs
  (FastRouteRenderer among them): 16 total. FastRoute is bundled
  inside grt (src/fastroute, 13 library files counting headers,
  8 compiled TUs, BSD-3, Iowa State) and is part of the grt
  import, not a system package.
- drt is the largest engine and the only one with a distributed
  subcomponent: its distributed/ headers and the FlexDR/FlexPA
  TUs include dst (Section 5a). One drt file,
  src/db/tech/frConstraint.cc, carries no licence header; it is
  covered by the directory-level LICENSE like the rest of drt.
  The 9 passing drt TUs are plain data-object files (drShape,
  frTime, frInst, frInstTerm, frNode, frRPin, taShape,
  frBaseTypes) that do not pull in the enum-formatting headers.
- rcx's library target is rcx_lib (17 TUs); it is the only engine
  whose link closure is already in tree (odb + utl, plus OpenSTA
  for the wrapper TU), and it passes whole.
- pdn and fin link the gui stub only for rendering hooks; neither
  needs Qt. Both pass whole.

## 3. Probe method and results

Method: for each engine, every library TU from its upstream
CMakeLists was compiled with g++ -std=c++20 -fsyntax-only
-fopenmp against the in-tree headers
(src/db/odb/include, src/db/odb/include/odb for the bare odb
headers such as db.h and dbUtil.h, and
src/analysis/timing/sta/include plus the generated
build/src/analysis/timing/sta/include/sta dir for the bare
StaConfig.hh that public sta headers include), with
not-yet-imported support-module headers taken from the pin tree
as read-only stand-ins (Section 5). C++20 because
eda_analysis_sta sets PUBLIC cxx_std_20 (Section 5c); the pin
itself is C++17. System headers came from the sandbox after
installing the CI set plus liblemon-dev (Section 6). The probe
script is workspace scratch, not a repo file; its per-TU output
was recorded in the wake log for this unit.

Results (152 TUs total, 75 PASS, 77 FAIL). Every FAIL has exactly
one of seven named causes; there is no eighth cause hiding in the
list, and no TU failed on a missing in-tree odb header:

| Cause | FAILs | TUs |
|---|---|---|
| fmt 9.1 rejects scoped-enum formatting (Section 5c) | 62 | drt 61, grt FastRoute utility.cpp 1 |
| sta/UnorderedSet.hh missing, via rsz/Resizer.hh (Section 5b) | 9 | gpl replace.cpp, timingBase.cpp; cts CtsGraphics, HTreeBuilder, LevelBalancer, SinkClustering, TechChar, TreeBuilder, TritonCTS (7) |
| sta/Vector.hh missing (Section 5b) | 1 | ifp InitFloorplan.cc |
| sta/Set.hh missing (Section 5b) | 2 | grt GlobalRouter.cpp and MakeWireParasitics.cpp (via MakeWireParasitics.h) |
| sta escapeChars signature drift (Section 5b) | 1 | cts Clock.cpp |
| LEMON 1.3.1 vs C++20 (Section 6) | 1 | cts Clustering.cpp |
| OR-Tools headers absent (Section 6) | 1 | gpl mbff.cpp |
| Total FAIL | 77 | |

Reading the results: the odb surface is fully compatible: no TU
failed on any in-tree odb header, so the version-skew rule is
satisfied for OpenDB, the pin the phase is built on. The sta
surface is not: five headers plus one signature drift (Section 5b).
The fmt surface is not: system fmt 9.1 cannot format the scoped
enums drt and FastRoute pass to Logger::report (Section 5c). Both
gaps are measured, bounded, and assigned to the stage unit that
must close them (P1 for Vector, P2 for OR-Tools, P3 for the cts
causes, P4 for Set, P5 for fmt and dst). The three engines whose
closures are already in tree (dpl, rcx, pdn) plus fin pass whole,
which corroborates that the harness itself is sound: it is not
failing engines indiscriminately.

## 4. Licence audit

Method: every compiled library source was scanned for GPL, LGPL,
AGPL, MPL, and EPL licence text, and for MIT, Apache, and Boost
licence text; every LICENSE/COPYING file under the nine
directories was read. Verdict: BSD-3-Clause only, no copyleft
change. The copyleft set stays exactly the Phase 0 set (OpenSTA
GPL-3.0, KLayout GPL-3.0, Netgen GPL-1.0); no engine adds a
copyleft component, and no licence commitment is made by this
unit.

- Directory LICENSE files (all BSD-3-Clause): gpl, dpl, cts, grt,
  drt, fin, plus grt/src/fastroute (BSD-3-Clause, Iowa State).
- ifp, rcx, and pdn carry no directory LICENSE file; every one of
  their library sources carries the BSD-3-Clause header text, so
  the verdict for them is the same, by header rather than by file.
- No compiled library source mentions GPL, LGPL, AGPL, MPL, EPL,
  MIT, Apache, or Boost licence text. The one headerless file
  (drt src/db/tech/frConstraint.cc) is covered by drt's
  directory LICENSE.
- Excluded from the verdict (not compiled): test/ trees, doc/,
  bench/, rcx calibration/ and example/ data, and the drt
  "Orientations to Use NDRs" PDF. If a stage unit later compiles
  any of these, it must audit them first.

## 5. Dependency closure: what the stage units must import

### 5a. Support modules



The nine engines do not link against odb and sta alone. Their
upstream link closures name support modules that are neither in
tree nor in the P0 list. Each rides with the first stage unit
that needs it, with its own PROVENANCE.md inventory, in the same
pattern as the odb re-pin. Nothing here is imported by P0.

| Support module | Size (lib C++ files) | Needed by | First-need unit |
|---|---|---|---|
| dbSta (sta/dbSta.hh, dbNetwork.hh) | 16 | ifp, gpl, cts, grt | P1 (ifp) |
| stt (SteinerTreeBuilder.h) | 10 | cts, grt, drt | P3 (cts) |
| ant (AntennaChecker.hh) | 4 | grt | P4 (grt) |
| dst (Distributed, JobMessage) | 17 | drt (core TUs: FlexDR, FlexPA, TritonRoute) | P5 (drt) |
| upf (upf.h) | 2 | ifp | P1 (ifp) |
| rsz (Resizer.hh) | 23 | gpl, cts, grt (repair/resize calls) | P2 (gpl) |
| gui stub (stub.cpp, stub_heatMap.cpp) | 2 | gpl, dpl, cts, grt, drt, pdn, fin | P1 (first engine import) |

Two further glue items, both EOS-owned code (never upstream
patches):

- ord shim: every engine includes ord/OpenRoad.hh and calls
  ord::OpenRoad accessors (getDb, getDbNetwork, getTritonCts,
  getReplace, getGlobalRouter, getPdnGen) and free functions
  (microns_to_dbu, get_db_block, get_db_tech, db_has_tech,
  ensure_linked). EOS must provide an ord-compatible surface
  backed by the session before the first engine import links.
  Design is P1 work; P0 only records the surface (27 includes of
  ord/OpenRoad.hh across the nine engines).
- CMake export gaps: two in-tree targets keep engine consumers'
  headers PRIVATE today. eda_db_odb keeps include/odb PRIVATE,
  but engines include bare odb headers (db.h, dbUtil.h, gseq.h,
  parse.h, util.h, array1.h) that live only there. eda_analysis_sta
  keeps its generated include/sta/StaConfig.hh dir PRIVATE, but
  public sta headers (Delay.hh) include StaConfig.hh. The first
  importing unit must promote both dirs to PUBLIC on their
  targets (EOS-owned CMake, not an upstream patch), or its build
  fails on headers EOS already ships.

Open question carried to P2 (not decided here): rsz is needed by
gpl, cts, and grt, but the resizer track is stopped at R0 and the
resizer is explicitly not unlocked. P2 must either import rsz_lib
as a support module (a scope decision for the owner), or show a
linkable partition that excludes the rsz calls. P0 does not decide
this; P1 (floorplan) does not need rsz and is unaffected.

### 5b. STA-version gap: five headers missing in-tree

In-tree sta is OpenSTA commit 0f2529d; the pin vendors OpenSTA
7a184d00. Of the 31 sta/*.hh headers the nine engines and their
support headers reference, 26 exist in-tree and 5 do not:

| Missing header | Referenced by | Contents / deps |
|---|---|---|
| sta/Vector.hh | ifp InitFloorplan.cc | Self-contained template (std::vector + Iterator, in-tree) |
| sta/Set.hh | grt GlobalRouter.cpp, MakeWireParasitics.h | Self-contained (std::set only) |
| sta/UnorderedSet.hh | via rsz/Resizer.hh (gpl replace.cpp and timingBase.cpp, cts 7 TUs) | Self-contained (std::unordered_set only) |
| sta/Corner.hh | cts TechChar.h, grt MakeWireParasitics.cpp | 133 lines; needs Vector.hh, StringSet.hh (also missing, see below), GraphClass, SearchClass, StaState (all three in-tree) |
| sta/PathAnalysisPt.hh | cts TechChar.cpp | 58 lines; needs Iterator, MinMax, SdcClass, SearchClass (all in-tree) |

Transitive sixth header: sta/StringSet.hh (a Set typedef plus a
deleteContents declaration) is missing in-tree too, but nothing
references it directly; it is needed only if Corner.hh is
provided. In-tree AnalysisCorner.hh is not a substitute: it
defines AnalysisCorner, not the pin-era Corner/Corners classes.

Signature drift beyond missing headers (one case): sta::escapeChars
in ParseBus.hh. The pin declares it as (const char*, ch1, ch2,
escape), escaping two characters; in-tree declares it as
(string_view, ch1, ch2, ch3, escape), escaping three. cts
Clock.cpp calls the 4-argument pin form, which has no in-tree
overload, so it fails to compile. Unlike a missing header, this
cannot be fixed by supplying a header alone: the importing unit
must also define the 4-argument overload (EOS-owned code, pin
semantics: escape ch1 and ch2) in its own module, the same
pattern as the ord shim. No upstream file is modified either way.

Resolution path (recommended, not yet attempted): the importing
units provide EOS-owned compatibility shims inside their own
modules (pin-era API surface, marked as shims, not upstream
files and not modifications of the sta module). The shim contents
for Vector, Set, and UnorderedSet are trivially small templates;
Corner and PathAnalysisPt must be proven against the in-tree
StaState/Graph/Search/Sdc classes by compilation, which is the
importing unit's experiment (P1 proves Vector only; P2-P4 prove
the rest as they land). If any experiment shows API drift beyond
headers (missing methods, changed signatures), that unit stops
and escalates: the fallback is re-pinning EOS sta to 7a184d00,
which would re-open the Phase 2 STA evidence and is a
structural-scale decision for the owner, not something this phase
decides alone. Note the precedent: the resizer track stopped on
exactly this class of skew (five-year OpenSTA drift). P0 measures
it; it does not repeat it quietly.

### 5c. fmt-version gap: scoped enums under fmt 9.1

drt (61 TUs) and FastRoute's utility.cpp format scoped enums
(frMinimumcutConnectionEnum, frCornerTypeEnum, grt::RouteType,
and others) through utl::Logger::report. Under system fmt 9.1
(Ubuntu noble, FMT_VERSION 90100) that fails at compile time:
fmt 9 has no formatter for scoped enums. The in-tree Logger.h is
byte-identical to the pin's, proven by SHA-256, so the fmt version
underneath is the only variable; a minimal scoped-enum format
repro fails identically under system fmt 9.1. Upstream pins fmt
8.1.1 on macOS with the comment "because fmt 9 causes compile
errors" (etc/DependencyInstaller.sh, which also pins spdlog
1.9.2), so the upstream-supported version is OLDER than the
system one, not newer. P5's candidate is therefore fmt 8.1.1
(source build; noble apt has only 9.1), to be proven by compiling
drt against it; P0 does not claim 8.1.1 works until that compile
passes. Until then, drt and FastRoute cannot compile against
system fmt, and no drt result in this document depends on working
around that: the 9 passing drt TUs are the ones that never format
an enum.

Also recorded: pd modules inherit C++20 from eda_analysis_sta,
which sets PUBLIC cxx_std_20 (its ContainerHelpers.hh uses
concepts). The pin itself is C++17, but engine consumers of
in-tree sta compile as C++20. No engine source required a newer
standard than that.

## 6. System dependencies

Already declared as EOS hard dependencies (cmake/EosDependencies.cmake
and the CI apt list) and therefore already satisfied: Eigen3
(gpl), Boost headers (dpl, cts, grt, pdn, fin), zlib (drt),
Tcl headers (gui stub), OpenMP (compiler flag, gpl and drt),
Threads. Qt is NOT needed: the gui stub path is used and gui.h
hides Qt from engine clients by design.

New system dependencies, declared here the way Boost was declared
in PR 40 (manifest row plus this inventory; configure checks and
CI install land with the first importing unit, because the CI
workflow file is structural and its change needs the owner's
approval, which P0 does not presume):

| Dependency | Used by | Install route | First-need unit |
|---|---|---|---|
| LEMON graph library | cts Clustering.cpp (1 TU, core path) | apt liblemon-dev (Ubuntu noble, verified present); on macOS upstream uses the tap The-OpenROAD-Project/lemon-graph/lemon-graph (LEMON is not in homebrew-core) | P3 |
| OR-Tools | gpl mbff.cpp (1 TU, called by Replace::runMBFF) | No Ubuntu apt package (verified: no candidate in noble). Upstream's DependencyInstaller.sh downloads the prebuilt C++ tarball or-tools 9.5.2237 from GitHub releases (default prefix /opt/or-tools, brew prefix on macOS); P2 pins that version and the install route | P2 |
| Boost serialization library | drt distributed headers and FlexDR/FlexPA (headers resolve under libboost-dev; the link library needs libboost-serialization-dev) | apt libboost-serialization-dev (headers already present; link package to add at P5) | P5 |
| fmt 8.1.1 (candidate) | drt (61 TUs) and grt FastRoute utility.cpp (Section 5c) | Noble apt has only 9.1, proven insufficient; upstream pins 8.1.1 on macOS because fmt 9 causes compile errors. P5 proves 8.1.1 by compiling drt and pins the source-build route | P5 |

LEMON/C++20 interaction (P3 must resolve, not assumed away):
LEMON 1.3.1 fails under C++20 (its array_map.h uses
std::allocator::destroy, removed in C++20), while pd modules
inherit C++20 from eda_analysis_sta. Clustering.cpp itself
includes no sta headers (Clustering.h, lemon headers,
utl/Logger.h only), so the candidate fix is a per-source
C++17 override for that TU in EOS-owned build glue, to be proven
at P3 import time. If Clustering.cpp's transitive includes do
reach sta concepts headers, that fix fails and P3 escalates with
the evidence; a newer LEMON is the fallback, not a presumption.

No other new system dependency was found. In particular: no CUDA
(gpl GPU paths are compile-guarded off), no SWIG (EOS builds no
upstream bindings; Make* glue is excluded), no Python at build
time for the engines.

## 7. What P1 must do first

1. Promote the two CMake export dirs (Section 5a) to PUBLIC.
2. Provide the ord shim surface backed by the session.
3. Provide the sta/Vector.hh shim ifp needs (Section 5b) and prove
   it by compiling ifp; escalate if API drift beyond headers
   appears. The remaining shims land with the units that need
   them (P2-P4), not all in P1.
4. Import dbSta, upf, and the gui stub alongside ifp, each with
   PROVENANCE.md, byte-verified against the pin.
5. Then implement eos::floorplan on the imported ifp. The rsz
   question (Section 5a) does not gate P1.

## 8. Scope compliance

- No engine code imported, no stage command registered, no
  structure.yaml change, no module adoption change. The pd and pv
  skeletons are untouched.
- No spend, no publication, no licence commitment.
- Manifest change only: a new openroad-pd-pv row at ead3236
  recording this evaluation; the existing openroad-modules row
  (v2.0, for the planned openroad-syn adoption) is unchanged.
