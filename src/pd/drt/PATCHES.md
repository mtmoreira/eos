# drt: port adaptations (EOS-owned changes to the imported copies)

The pin tree is never modified. Every change below lives only in
the EOS port copies in this module, and is recorded here so the
difference from upstream ead3236 is auditable. Nothing else in
this module differs from upstream except the EOS-owned files
(CMakeLists.txt, README.md, PROVENANCE.md, this file, and the
internal/dst shim headers) and the port copies listed below
(TritonRoute.cpp, RoutingCallBack.h, FlexDR.cpp, frBaseTypes.h).

## 1. No Tcl factory, no Tcl init (TritonRoute.cpp)

Upstream TritonRoute::init defines SWIG Tcl commands through
Drt_Init and sta::drt_tcl_inits (the Tcl files encoded from
TritonRoute.tcl). EOS registers commands through its own registry
and constructs TritonRoute directly, so the port copy drops both
calls and their declarations. No EOS command needs a drt Tcl
command: the runner calls init/main/stepDR/endFR directly. The
sta/StaMain.hh include stays (the header exists in-tree) but is
now unused by this file.

## 2. No distributed routing (TritonRoute.cpp, RoutingCallBack.h, internal/dst/)

Upstream drt links the dst distributed-routing engine
(Distributed, JobMessage, JobCallBack, BalancerJobDescription).
EOS runs single-host only, so dst is NOT imported. Instead this
module provides fresh EOS-owned shim headers under internal/dst/
(JobMessage.h, JobCallBack.h, Distributed.h,
BalancerJobDescription.h), written in EOS style, not copied from
upstream: they declare exactly the API surface drt references on
distributed paths. Distributed's methods throw std::runtime_error
("distributed routing is not supported in EOS"), so a distributed
call fails loudly instead of silently doing nothing; the runner
never enables distributed mode and always passes a null dist, so
these paths are link closure for dead code, never executed.

The port copy of TritonRoute::init guards the callback
registration on a non-null dist: with a null dist no
RoutingCallBack is created. RoutingCallBack.h itself is an
upstream header (part of drt's distributed/ directory) and is
otherwise unchanged except for section 3.

## 3. ord shim has no thread count, file reader, or file writer (TritonRoute.cpp, RoutingCallBack.h, FlexDR.cpp)

The in-tree ord::OpenRoad shim (dbsta module) carries no
getThreadCount, readDb, or writeDb, and this unit does not change
dbsta (the brief's stop rule). The port copies adapt instead:

- Thread count: TritonRoute.cpp gains a file-local
  eosThreadCount() (std::thread::hardware_concurrency, falling
  back to 1) replacing all five getThreadCount call sites
  (updateDesign x2, main, pinAccess, and the distributed
  updateDesign path). RoutingCallBack.h replaces its two
  getThreadCount call sites with the same hardware-concurrency
  expression inline (it is a header, so it carries its own
  <thread> include). Thread counts only size OpenMP teams; the
  routing result does not depend on the exact count.
- resetDb (TritonRoute.cpp, distributed-only): the readDb call is
  replaced with Logger::error DRT 900 (noreturn): the EOS session
  holds the database in memory and never re-reads it from a file.
- sendDesignDist writeDb (TritonRoute.cpp, distributed-only):
  replaced with Logger::error DRT 901.
- Debug dumps (TritonRoute.cpp main, FlexDR.cpp main snapshot):
  the writeDb calls are debug-only dumps (debugDumpDR /
  "snapshot" debug check, never set by the EOS runner). The port
  copies replace them with Logger::warn DRT 902 / 903 and let the
  run continue: skipping a debug dump must not fail a real run.

After these changes no port copy references ord::OpenRoad at all
except the ord/OpenRoad.hh include that RoutingCallBack.h still
carries (harmless: the header exists in-tree).

## 4. fmt formatters (frBaseTypes.h)

System fmt 9.1 no longer formats operator<< types automatically
(pin-era fmt 8 did), so Logger calls that format the fr enums
fail to compile (first seen in frConstraint.h report calls with
frMinimumcutConnectionEnum and frCornerTypeEnum). The enums
already have operator<<, so the port copy of frBaseTypes.h gains
explicit `fmt::formatter` specializations delegating to
`ostream_formatter` for all five enums that declare operator<<
(frBlockObjectEnum, frConstraintTypeEnum, frCornerTypeEnum,
frMinimumcutConnectionEnum, frMinstepTypeEnum), the same class
of fix as the grt DataType.h precedent. No log call is reworded
and no computed value changes. The `<fmt/ostream.h>` include
sits outside namespace fr so its contents land in fmt rather
than fr::fmt. Extended 2026-11-02 (driver wake) with the same
pattern for four more operator<< types whose Logger calls failed
the same way: fr::frTime (FlexDR.cpp), fr::frPathSeg
(FlexDR_conn.cpp), odb::Rect and odb::Point
(io_parser_helper.cpp). frTime and frPathSeg are forward-declared
in frBaseTypes.h (their defining headers are not included there);
their operator<< declarations reach each logging TU through that
TU's own includes (frTime.h, global.h, odb/geom.h).

## 5. Not imported

- MakeTritonRoute.cpp: Tcl/SWIG factory, not imported. The EOS
  runner constructs triton_route::TritonRoute directly.
- All .tcl and .i (SWIG) surfaces, the upstream test directory,
  and upstream CMakeLists files: EOS writes its own CMakeLists.
- dst (Distributed.cc, JobMessage.cc, workers, load balancer):
  not imported; see section 2 for the shim that replaces it.

## 6. Build notes (EOS-owned, not engine changes)

- Boost serialization and OpenMP are required by the upstream
  sources (binary archives for worker serialization, omp pragmas
  in FlexDR/FlexPA). They are declared external dependencies in
  this module's CMakeLists (Boost COMPONENTS serialization,
  OpenMP::OpenMP_CXX) and in third_party_manifest.yaml; no Boost
  or OpenMP sources are vendored. ZLIB is linked as upstream
  links it; zlib1g-dev is already in the CI apt list.
- The graphics files (FlexDR_graphics, FlexTA_graphics,
  FlexPA_graphics) compile against the dbsta gui stub header and
  are inert at runtime (FlexDRGraphics::init checks
  gui::Gui::enabled(), false under the stub).
