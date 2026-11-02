# cts: port adaptations (EOS-owned changes to the imported copies)

The pin tree is never modified. Every change below lives only in
the EOS port copies in this module, and is recorded here so the
difference from upstream ead3236 is auditable. Nothing else in
this module differs from upstream except the EOS-owned files
(src/cts.cc, src/cts_resizer.cc, include/eos/pd/cts.h,
CMakeLists.txt, README.md, PROVENANCE.md, this file) and the
port copies listed below (TritonCTS.cpp, TechChar.cpp/.h,
Clock.cpp, Util.h).

## 1. Corner to Scene (TritonCTS.cpp, TechChar.cpp, TechChar.h)

The pin-era STA API names its analysis corner `sta::Corner`
(`cmdCorner()`, `findPathAnalysisPt`, `Corners`, `PathAnalysisPt`);
the in-tree STA renamed it to `sta::Scene` (`cmdScene()`,
`Scene::dcalcAnalysisPtIndex`). The port copies adapt
mechanically, with no change to what CTS computes:

- TritonCTS.cpp, three sites (selectRootBuffer,
  selectSinkBuffer, computeInsertionDelay): `sta::Corner* corner
  = openSta_->cmdCorner()` becomes `const sta::Scene* scene =
  openSta_->cmdScene()`, and the resizer wire-RC calls take the
  scene (the in-tree resizer takes `const Scene*`, not
  `Corner*`).
- TechChar.cpp initClockLayerResCap: same change for the clock
  wire-RC calls.
- TechChar.h: `#include "sta/Corner.hh"` becomes
  `#include "sta/Scene.hh"`; `charCorner_` (Corner*) becomes
  `charScene_` (Scene*); `charPathAnalysis_` (PathAnalysisPt*)
  becomes `charDcalcAp_` (DcalcAPIndex, from
  `Scene::dcalcAnalysisPtIndex(MinMax::max())`).
- TechChar.cpp computeTopologyResults: pin-era
  `vertexArrival(vertex, RiseFall, PathAnalysisPt*)` becomes
  in-tree `arrival(vertex, RiseFallBoth, SceneSeq{charScene_},
  MinMax::max())`, and `vertexSlew(vertex, RiseFall, MinMax*)`
  becomes `slew(vertex, RiseFallBoth, SceneSeq{charScene_},
  MinMax::max())`. Arrival and Slew are Delay values, which
  convert to float as before. `power(instance, Corner*)` becomes
  `power(instance, Scene*)`, and `setAnnotatedSlew(vertex,
  Corner*, ...)` becomes `setAnnotatedSlew(vertex, Scene*,
  ...)`, both existing in-tree signatures. The `#include
  "sta/PathAnalysisPt.hh"` is dropped: the in-tree STA has no
  such header, and nothing in the port copy references the type
  anymore.
- TechChar.cpp axis lookup: pin-era `TableAxis::values()`
  returns a `FloatSeq*`; in-tree it returns a `const FloatSeq&`.
  The port holds pointers to that reference, so all downstream
  uses (null checks, size, indexing) are unchanged.

## 2. Scratch STA without makeBlockSta (TechChar.cpp, TechChar.h)

The pin-era dbSta has `makeBlockSta(block)`, which the
characterization uses to build a scratch STA for its
characterization block. The in-tree dbSta has no such helper,
so the port copy builds the scratch STA by the same recipe the
in-tree resizer uses for its own scratch STA: a fresh dbSta
with `makeComponents()`, its own Tcl interp (ReportTcl
teardown touches the interp channel stack, so the interp must
be real and must outlive the STA), the main logger for error
reporting, the characterization block on its dbNetwork, the
main network's default Liberty library (exactly as pin
makeBlockSta propagates it), and units copied from the main
STA. Full dbSta::init is NOT used: it would hijack the global
Sta singleton from the main STA session. The previous scratch
STA is reset (and its interp deleted) before a new one is
built, because create() calls this once per wirelength; a
`~TechChar()` added by the port deletes the last iteration's
interp after resetting its STA. No Liberty is re-read into
the scratch STA: cell data resolves through the shared
database objects (the in-tree dbNetwork maps db objects to
STA objects by identity) plus the propagated default library.

## 3. Resizer/stt header split: EOS-owned unit

As in place, the resizer header pulls in the pdr FLUTE
definitions, which must never share a translation unit with
the ead3236 Steiner-tree header. The CTS runner needs both
engines, so the resizer construction lives in its own
EOS-owned unit (src/cts_resizer.cc, declared in
src/cts_resizer.h with only a forward-declared rsz::Resizer),
and the runner holds the result as an opaque pointer. No
upstream file is changed, no engine is stubbed: the real
resizer is constructed and initialized, just in a separate
unit.

## 4. escapeChars arity (Clock.cpp)

The pin-era STA `escapeChars` takes four arguments (token, ch1,
ch2, escape) and inserts the escape before ch1 or ch2. The
in-tree STA takes five (token, ch1, ch2, ch3, escape). The
Clock constructor's call escapes '/' with '\\' and leaves ch2
unused ('\0'), so the port copy passes '\0' as ch3: same
escaping, same result, no behavior change.

## 5. fmt formatters (Util.h)

System fmt 9.1 no longer formats operator<< types automatically
(pin-era fmt 8 did), so Logger calls that format a Point or Box
fail to compile. Point and Box already have operator<<, so
Util.h gains explicit `fmt::formatter<cts::Point<T>>` and
`fmt::formatter<cts::Box<T>>` specializations delegating to
`ostream_formatter`, the same class of fix as the pdr and grt
precedents. No log call is reworded and no computed value
changes.

## 6. Resizer/dbSta forward declarations (TechChar.h)

TechChar uses Resizer and dbSta only as pointers (constructor
arguments, members, and a unique_ptr member whose destructor is
defined in TechChar.cpp where dbSta is complete), so the port
copy forward-declares both instead of including
`rsz/Resizer.hh`. Including it would pull the pdr FLUTE
definitions into every unit that includes TechChar.h, and
FLUTE's `stt::Branch`/`stt::Tree` typedefs collide with the
ead3236 Steiner-tree header that SinkClustering.cpp must also
include (same class of collision as the cts_resizer split in
section 3). Resizer.hh also used to supply the dbSta
declaration, so that is forward-declared too. The units that
call Resizer or dbSta methods, TechChar.cpp and TritonCTS.cpp,
include those headers directly, so no call site loses the
definition.

## 7. STA Sdc access and clock-tree-path delays (TritonCTS.cpp)

The pin-era STA exposes the active SDC through
`StaState::sdc()`, which the in-tree STA does not carry: the
in-tree `Sta::cmdSdc()` returns `cmd_mode_->sdc()`, the same
command-mode SDC object the pin-era accessor returned for the
active state. The port copy therefore calls `openSta_->cmdSdc()`
at the two `sdc()` sites (leaf-pin clock check, clock
enumeration). No clock set changes: the same Sdc, the same
clocks, the same `isLeafPinClock` query.

The in-tree `Sdc::clocks()` returns `const ClockSeq&`, not a
pointer, so the enumeration iterates `sdc->clocks()` directly
instead of dereferencing it. Same sequence, same order.

The in-tree `LibertyPort` has no `clockTreePathDelays()`
(RiseFallMinMax over min and max clock-tree-path arcs). Its
`clkTreeDelay(in_slew, from_rf, to_rf, min_max)` evaluates the
same arcs: it selects arc sets whose role is
`clockTreePathMin`/`clockTreePathMax` and evaluates the gate
table model at the given slew with zero load. The upstream
helper evaluated its gate models at slew 0 and zero load, so
the port copy calls `clkTreeDelay(0.0, rf, MinMax::max())`,
reproducing the upstream evaluation point exactly. Only the
max rise and max fall values are consumed at this call site
(their average becomes the insertion delay per second), so
only those two are populated, and only where a matching
max-role arc exists for that edge, which preserves the
upstream `hasValue()` gate: with no clock-tree-path arcs the
result stays empty and the function returns 0.0 exactly as
before. The unused min values are dropped, not approximated.

## 8. Not imported

- MakeTritoncts.cpp: Tcl/SWIG factory, not imported. The EOS
  runner constructs TritonCTS directly and the EOS registry
  registers eos::cts.
- CtsGraphics.cpp/.h: GUI renderer (gui::Gui), not imported.
- All .tcl and .i (SWIG) surfaces, the upstream test
  directory, and upstream CMakeLists files: EOS writes its own
  CMakeLists.

## 9. Runner notes (EOS-owned, not engine changes)

- src/cts.cc is EOS-owned: it constructs the engines, loads
  Liberty into its private STA session, selects the clock, runs
  TritonCTS, and reports roots/buffers/sinks/subnets from the
  engine. It does not modify engine behavior.
- The runner constructs a fresh sta::dbSta session for the run
  and clears the Sta singleton (Sta::setSta(nullptr)) before
  returning, on both success and failure, so a later run_timing
  or a second cts in the same session never dangles on the
  destroyed session.
- The runner's STA session uses a private Tcl interp
  (Tcl_CreateInterp plus Tcl_Init, deleted after the session):
  dbSta::init routes reporting through ReportTcl, whose
  setTclInterp and destructor touch the interp's channel stack,
  so a null interp segfaults in Tcl_GetThreadData (observed in
  the place unit test). Same pattern as the place runner.
- Clock selection is explicit (-clock) or from SDC clocks in
  the session; the runner fails fast on no clock because the
  engine only warns. A Liberty path is required up front
  because TechChar errors without a buffer list and a buffer
  list without Liberty cells cannot be characterized.
- Skew and insertion delay in ns are not engine outputs:
  TritonCTS insertion delay is a length, not a time, and it
  computes no ns skew. The report flags both unavailable with
  that reason; the eos::cts command appends post-CTS STA slack
  from the session timing service instead. Nothing is invented.
- LEMON: Clustering.cpp is the only CTS unit that includes
  LEMON headers and it includes no STA headers, so just that
  unit compiles at gnu++17 (LEMON 1.3.1 predates the C++20
  removal of std::allocator::construct/destroy). Same reason
  as the eda_pd_stt precedent; no imported file is patched.
