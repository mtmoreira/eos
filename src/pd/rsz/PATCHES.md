# rsz module: upstream adaptations (EOS-owned record)

Upstream sources are never patched. Every change below is either
an adaptation applied at import time (recorded here) or an
EOS-owned file (CMakeLists.txt, README.md, this file,
PROVENANCE.md, the unit test). Upstream pin: OpenROAD v2.0,
commit a5a56584 (candidate tree). The port copies this module
is built from live in the R1 spike port tree
(eos-rsz-spike/rsz-spike-port/rsz), which carries the patch
list P-0 through P-32 summarized below. The full running
record is that tree's PATCHES.md; this file is the import
disposition, not a second copy of every line.

## PR3-only adaptations (import time, not in the port list)

- SteinerTree.hh: `#include "flute.h"` becomes
  `#include "pdr/flute.h"`. The bare name does not resolve
  through any rsz include path; pdr exposes FLUTE at
  include/pdr/flute.h. Include-path adaptation only, no
  behavior change.
- R-1 (grt decoupling): Resizer.cc's
  `#include "grt/GlobalRouter.h"` is deleted, and
  Resizer::init's `GlobalRouter *grt` param becomes
  `float routing_alpha`, stored as routing_alpha_ and
  returned by routingAlpha(). The ctor defaults
  routing_alpha_ to 0.3, the v2.0 GlobalRouter default
  alpha. Reason: grt is not an EOS module (P2 imports it
  as support later), and plan 07's rsz dependency list
  omits grt; the port's grt include is a missed
  dependency of the same class as the stage-1
  MakeDatVar miss. The engine uses grt for exactly one
  thing, getAlpha() via routingAlpha(), consumed only
  by the two makeSteinerTree call sites. Passing the
  alpha value instead of a router pointer keeps the
  module within its verdict dependencies (dbsta, pdr,
  odb, sta) without importing grt piecemeal and without
  a fake grt declaration that would collide with the
  real P2 import. Callers that later own a router pass
  its alpha; the PR4 command passes the session alpha
  (default 0.3). The PR3 repair driver passes 0.3, the
  same value the step-1 stub used, so the END metrics
  comparison is apples to apples. Same class as P-13,
  which already changed init's signature (dropping
  OpenRoad* and Gui* params).
- R-2 (SWIG init drop): Resizer::init's interp branch
  (Rsz_Init plus evalTclInit over sta::rsz_tcl_inits)
  is deleted, with the extern declarations and the
  evalTclInit using-declaration. Reason: Resizer.i is
  not imported, so those symbols have no definition in
  EOS; linking any consumer of the module failed on
  them even though the branch never executes when
  interp is null (the driver and the PR4 command pass
  the session interp through init's API, but upstream
  command registration must not run). The interp param
  is retained for the session API. dbsta M-3 precedent
  (Dbsta_Init plus evalTclInit dropped for the same
  reason). The scratch-sta interp in cellWireDelay
  (P-30, real Tcl C API) is untouched.
- CMakeLists.txt is EOS-owned (upstream rsz/CMakeLists.txt
  carries a GPL header comment and is never imported).
- Resizer.tcl, Resizer.i: not imported (EOS registry
  replaces the upstream Tcl/SWIG surface).
- Unused.cc: not imported (dead upstream code, never
  compiled).

## Port patch disposition (P-0 through P-32)

- P-0 (mechanical): opendb/ include prefix becomes odb/
  (SteinerTree.hh geom.h). EOS's ead3236 odb exposes odb/.
- P-1 (dbSta only): dbStaReport printf-style virtuals become
  2026 *Msg hooks. Lives in the merged dbsta module (PR1),
  not here.
- P-2 (mechanical): PathRef becomes Path (2026 STA removed
  PathRef.hh); StaState* threading at call sites.
- P-3 (dbSta only): dbSta GUI plus ord app-wiring strip.
  Lives in the merged dbsta module (PR1), not here. The rsz
  side keeps the matching strips: Resizer.hh drops OpenRoad*
  and Gui* params and members; SteinerRenderer /
  highlightSteiner deleted (P-22).
- P-4 (dbSta only): dbSta::netSlack becomes Sta::slack.
  Lives in the merged dbsta module (PR1), not here.
- P-5, P-6, P-10, P-25, P-26, P-27 (dbNetwork only): name()
  std::string, ConstNetSet becomes NetSet, const char*
  becomes std::string migration, makeLibrary nullptr
  filename becomes "", string_view virtual overrides,
  Pin/Instance id() overrides. All live in the merged
  dbsta module (PR1), not here. P-12 groupBusPorts
  predicate is SUPERSEDED by D-3 (PR1): P1's makeCell
  "assume msb first" treatment stands; the R1
  always-false predicate is not carried. Bus-ordering
  validation against liberty ordering rides with the
  adapted Tcl suite (next increment).
- P-7 (dbSta only): findClkNets over modes. Lives in the
  merged dbsta module (PR1), not here.
- P-8 (dbSta only): std:: qualification. Lives in the
  merged dbsta module (PR1), not here.
- P-9 (mechanical, carried): removed STA containers become
  std containers (sta::Map/UnorderedMap/UnorderedSet/Vector
  become std::map/unordered_map/unordered_set/vector);
  hasKey becomes count, member findKey becomes
  sta::findKey / findKeyValue free functions.
- P-11 (dbSta only): VerilogReader lifecycle explicit.
  Lives in the merged dbsta module (PR1), not here.
- P-13 (carried): rsz header Corner becomes Scene,
  DcalcAnalysisPt becomes DcalcAPIndex; hashPtr becomes
  std::hash; OpenRoad/Gui members removed; tgt_slew_corner_
  renamed tgt_slew_scene_; highlightSteiner decl removed;
  parasiticNodeConnectPins gains a Parasitics* param.
  MakeResizer.hh/cc: ord/OpenRoad and gui includes
  removed, initResizer body removed, make/delete
  factories kept (ord-namespace but OpenRoad-free).
- P-14 (carried): sta::stringPrintTmp becomes a file-local
  rotating temp pool in SteinerTree.cc (name() still
  returns const char*, called several times per statement).
- P-15 (carried): const Pin* propagation (2026 PinSeq is
  vector<const Pin*>); the single genuine mutation site
  (disconnectPin in rebufferTopDown) uses an explicit
  const_cast with a comment.
- P-16 (carried): printf %s sites pass .c_str() for
  std::string values (BufferedNet.cc report).
- P-17 (carried, VALIDATION OPEN): Rebuffer.cc scene port
  plus delayAsString arg order swap. pinCapacitance /
  bufferInputCapacitance map 2021 cornerPort(lib_ap)
  capacitance to 2026 port capacitance at the dcalc
  ap's min_max. Value-equivalence NOT proven: 2021
  selected a corner library's port, 2026 selects a
  min/max value within one port's table. Validated
  against repair_cap* and the gcd pin-cap spot values
  by the adapted Tcl suite (next increment).
- P-18 (carried, VALIDATION OPEN): EstimateWireParasitics
  scene port plus lazy parasitic reduction. reduceTo
  plus deleteParasiticNetwork are DELETED, not replaced:
  2026 delay calcs reduce lazily during delay
  calculation, and deleting the network would remove
  the estimated parasitics. Deepest semantic area;
  validated on gcd by step 1 (finite sane estimated-net
  delays, no inf/nan) and further against repair_wire*
  by the adapted Tcl suite (next increment).
- P-19 (carried, VALIDATION OPEN): Resizer.cc repair-path
  port. NonRegSearchPred is a local SearchPred0 subclass
  (refuses internal arcs of sequential cells); the 2026
  base no-mode searchThru loops over modes, so one
  override covers both paths. ClkArrivalSearchPred maps
  to ClkTreeSearchPred. isClockPin / isConstantPin /
  isDisabledConstraint become any-mode loops. checkSlew /
  checkCapacitance / checkFanout take the 2026 forms.
  Delay arithmetic goes through float intermediates
  (2026 Delay class has no arithmetic operators).
  GateTimingModel 5-arg default-dropping: the 2021
  8-arg calls (cell, pvt, in_slew, load_cap, 0.0, false,
  arc_delay, arc_slew) map to the 5-arg form; the
  trailing (0.0, false) args were always literal
  defaults at both call sites. FLAGGED as an assumption
  to validate against repair_setup* / repair_slew* by
  the adapted Tcl suite (next increment).
- P-20 (carried): cellWireDelay scratch-sta port (dbBlock
  4-arg create, scratch dbSta without Tcl/OpenRoad,
  makeComponents plus setBlock binding, per-main-scene
  parasitic build/delete, deleteInstance/deleteNet
  cleanup; dbBlock::destroy dropped, see P-22).
- P-21 (carried): makeWireParasitic gains a Network*
  param (the scratch network); 2026 2-arg
  makeParasiticNetwork, ensureParasiticNode, incrCap,
  makeResistor forms.
- P-22 (carried): SteinerRenderer plus highlightSteiner
  deleted (GUI only, no repair caller). Scratch-sta
  setLogger call dropped as unbuildable (dbStaReport is
  dbSta.cc-local); superseded by P-32. dbBlock::destroy
  dropped (ead3236 odb has no dbBlock destroy API):
  one empty scratch block leaks per cellWireDelay call,
  bounded by repair iterations, now with unique names
  per P-31.
- P-23 (carried, DORMANT, recorded): ArcDelayCalc::loadDelay
  is gone in 2026. Wire delay uses Parasitics::findElmore
  (exact for the single-segment pi network). Load slew
  has NO 2026 primitive and falls back to
  load_slew = drvr_slew, so findMaxSlewWireLength's
  limit is optimistic. MITIGATION: findMaxSlewWireLength
  is currently UNCALLED anywhere in the repair flow
  (verified by grep), so the approximation is dormant.
  R1 must re-derive load slew or accept the optimistic
  limit explicitly before any caller uses that function.
- P-24 (carried, VALIDATION OPEN): NonLatchSearchPred
  reconstruction (SearchPred0 subclass refusing internal
  arcs of latch cells, detected via isLatchDtoQ /
  isLatchEnToQ roles). Reasoned, not recovered: the
  candidate tree has no sta/ sources, so the exact 2021
  rule is not recoverable. Validated against repair_hold*
  (the family most sensitive to search-predicate shape)
  by the adapted Tcl suite (next increment); any
  behavioral delta is recorded there.
- P-28 (carried, adaptation not upstream behavior):
  graph refresh after buffer insertion in repairSetup
  (networkChangedNonSdc plus ensureGraph plus
  level-driver-cache reset per repair pass; plain
  networkChanged() rejected because it wipes SDC).
  Upstream v2.0 carries the same code shape with no
  refresh found, and whether upstream avoids the
  scenario some other way is UNPROVEN.
- P-29 (carried): InstanceSet/PinSet comparator binding
  (2026 sets bind a network-id comparator at
  construction; five bare sets rebound: 2 members in
  init, 3 locals).
- P-30 (carried, rule for consumers): scratch-sta Tcl
  interp. cellWireDelay creates its own interp
  (Tcl_CreateInterp plus Tcl_Init), calls
  setTclInterp, and deletes the interp after deleting
  the sta. The main interp is never shared (ReportTcl
  teardown calls Tcl_UnstackChannel unconditionally).
  Resizer.cc therefore includes <tcl.h>; the module
  CMakeLists keeps the dbsta-style TCL conditional
  include/link block.
- P-31 (carried): unique scratch block names
  (std::format "wire_delay{}" with a counter), because
  the leaked blocks of P-22 would otherwise collide on
  the second cellWireDelay call and setBlock(null)
  would segfault.
- P-32 (carried): dbSta::setReportLogger (port-owned
  glue on dbSta, not STA core); every scratch-sta
  construction site must call it. Lives in the merged
  dbsta module (PR1) as the method; the call site in
  cellWireDelay lives here. Supersedes P-22's claim
  that the scratch sta never reports (it does report
  on delay-calc errors, and a null logger segfaults
  inside errorMsg).
- P-33 (driver-only wire RC): becomes the Q6 policy,
  not a code patch. See README.md and the next
  increment (session/command layer).

## PR3 decisions (within verdict scope, documented here)

- repairHold staleness: repairHold keeps the upstream
  shape (no P-28 refresh inside repairHoldPass).
  repairHoldPass iterates a precomputed vertex list and
  inserts buffers without Steiner-tree vertex lookups,
  so the null-vertex crash class does not apply there.
  The outer repairHold loop re-times (findRequireds
  plus fresh findHoldViolations) at each pass boundary,
  so inserted buffers become visible to the next pass;
  staleness is confined within a pass. Quality question
  only, no crash class found; step-1 END hold is clean
  on gcd. Applying the refresh inside the pass would
  change behavior versus the validated step-1 run and
  is not done silently.
- ensure_linked / link_func_: untouched. No rsz source
  calls ensure_linked (verified by grep over the port
  tree), and the driver path binds the network via
  dbNetwork::init / setBlock without Tcl link_design.
  The P1 ord shim's ensure_linked placeholder therefore
  stands unchanged; the first engine that calls it must
  give it link-or-error semantics, and rsz is not that
  engine.
- Net/Term id(), dbSdcNetwork re-derivation,
  dbNetwork::setLogger, dbSta Tcl-init: closed by PR1
  (M-1, M-4, M-3), not re-opened here.
- D-8 liberty-loaded re-check, P-12 bus validation,
  P-17 / P-19 / P-24 semantic-mapping validation: ride
  with the adapted Tcl suite (next increment). They are
  acceptance items for the suite, not for this module
  import alone.
- pdr link-check: decided by the repair-driver link
  result in this increment (see README.md), not by fiat.
