# place: port adaptations (EOS-owned changes to the imported copies)

The pin tree is never modified. Every change below lives only in
the EOS port copies in this module, and is recorded here so the
difference from upstream ead3236 is auditable. Nothing else in
this module differs from upstream except the EOS-owned files
(src/place.cc, include/eos/pd/place.h, CMakeLists.txt, README.md,
PROVENANCE.md, this file).

## 1. gpl timingBase.cpp: plain Slack, not std::optional<Slack>

The in-tree resizer returns a plain Slack from resizeNetSlack;
pin-era gpl expects std::optional<Slack>. The port copy drops the
`.value()` calls at the two slack sites and the
`if (!net_slack_opt) { continue; }` + `net_slack_opt.value()`
block. No behavior change beyond the type: the in-tree resizer
always returns a slack value, so the absent-value branch could
never fire.

## 1b. gpl mbff.cpp: in-tree STA API port

Upstream mbff.cpp assumes the pin-era STA API in four places;
the in-tree STA differs, so the port copy adapts it (pin tree
untouched). All four adaptations are mechanical, with no change
to what MBFF computes:

- `lib_cell->sequentials()` in-tree returns a container of
  Sequential values (Sequential has a deleted copy constructor),
  not pointers. The loops iterate by `const auto&` and use `.`
  instead of `->`; the Sequential accessors used (preset, clear,
  clock, data) exist in-tree with the same names.
- `sta_->isClock(pin)` in-tree is `Sta::isClock(pin, mode)` and
  assumes `ensureClkNetwork(mode)` (rsz Resizer::isClockPin
  precedent). IsClockPin now returns true for a CLOCK sig-type
  iterm, false for a null STA pin, and otherwise true if the pin
  is a clock pin in any mode.
- The pin-era `sta::FuncExprPortIterator` is not in-tree;
  `FuncExpr::ports()` returns the same port set
  (LibertyPortSet). GetNumD counts the set's non-null ports and
  the D-pin collection pushes the set's ports; a null data
  function contributes nothing, as before.
- In-tree `odb::dbITerm::name()` returns std::string while
  `findITerm` takes `const char*`; the seven findITerm call
  sites add `.c_str()`.

## 2. Not imported

- MakeReplace.cpp, MakeOpendp.cpp: Tcl/SWIG factories, not
  imported. The EOS runner constructs Replace and Opendp directly
  and the EOS registry registers eos::place.
- gpuSolver.cu: excluded; the build is CPU-only. The initial
  placement path logs "GPU is not available. CPU solve is being
  used." when the GPU path is requested, matching upstream's
  CPU fallback.
- All .tcl and .i (SWIG) surfaces, and upstream CMakeLists files:
  EOS writes its own CMakeLists.

## 3. Resizer/stt header collision: EOS-owned split unit

The v2.0 resizer header (rsz/Resizer.hh, via rsz/SteinerTree.hh)
pulls in the pdr FLUTE header, which defines stt::Branch and
stt::Tree in the old array-based form, while the ead3236
Steiner-tree header (stt/SteinerTreeBuilder.h) defines
stt::Branch and stt::Tree in the new vector-based form. Both
headers in one translation unit is a hard compile error. The
place runner needs both engines, so the resizer construction
lives in its own EOS-owned unit (src/place_resizer.cc, declared
in src/place_resizer.h with only a forward-declared
rsz::Resizer), and the runner holds the result as an opaque
pointer. No upstream file is changed, no engine is stubbed: the
real resizer is constructed and initialized, just in a separate
unit. This collision is a consequence of mixing the v2.0 rsz with
the ead3236 stt; it will go away if rsz is ever re-pinned to the
ead3236 Steiner-tree API, at which point this split unit can be
folded back into place.cc.

## 4. Runner notes (EOS-owned, not engine changes)

- src/place.cc is EOS-owned: it constructs the engines, runs
  global then detailed placement, counts placed instances and
  pairwise bounding-box overlaps from the database, and runs the
  engine's own checkPlacement as the placement report. It does not
  modify engine behavior.
- The runner constructs a fresh sta::dbSta session for the run
  and clears the Sta singleton (Sta::setSta(nullptr)) before
  returning, on both success and failure, so a later run_timing
  or a second place in the same session never dangles on the
  destroyed session.
- Timing-driven mode fails fast when the STA session holds no
  Liberty library, because the resizer's slack path has nothing
  to read. Wiring Liberty into dbSta belongs to a later unit, not
  P2: read_liberty only stores a path in the session, it does not
  load Liberty into dbSta.
- The runner's STA session uses a private Tcl interp
  (Tcl_CreateInterp plus Tcl_Init, deleted after the session):
  dbSta::init routes reporting through ReportTcl, whose
  setTclInterp and destructor touch the interp's channel stack,
  so a null interp segfaults in Tcl_GetThreadData (observed in
  the place unit test and the registry place test run in
  isolation). Same pattern as the resizer scratch sta (P-30).
- Routability-driven mode is on only when the STA session holds
  a Liberty library. Without one, the global router's clock-net
  detection builds the STA graph and throws STA-2141 (observed
  in both place tests and the registry place test: Nesterov
  routability calls globalRoute, initClockNets calls
  findClkNets, ensureLibLinked throws). Without Liberty the
  runner runs plain Nesterov plus detailed placement instead;
  the router path itself is unchanged and stays live whenever
  Liberty is loaded.

## 5. gpl timingBase.cpp: format Delay mean in the worst-slack log

Mac-only build failure (owner's build-fresh eos-gui rebuild, after
PR 75 and PR 81): Homebrew fmt rejects the call at timingBase.cpp
line 165, log_->info(GPL, 100, "worst slack {:.3g}", slack_min),
because sta::Delay in this OpenSTA pin is a class (mean,
mean_shift, std_dev2, skewness) with no fmt formatter, and fmt
reports implicit instantiation of undefined template
fmt::detail::type_is_unformattable_for<sta::Delay, char>. The fmt
CI uses accepts the same line, which is why CI stayed green.
The port copy passes slack_min.mean() instead of slack_min.
Output is identical: Delay's own operator float() returns mean().
No change to the format string, the log id, or the message text.
Sweep of the whole place module (gpl, dpl, runner, headers):
this was the only logger or fmt call passing a sta::Delay or
other STA class value; every other formatted argument is an
integer, float, double, size, string, or coordinate, all
fmt-formattable.

## 6. gpl timingBase.cpp: format Delay mean in the net-slack debug print

Mac-only build failure (owner's build-fresh eos-gui rebuild of
main 2d69060): Homebrew fmt rejects the debugPrint call at
timingBase.cpp line 191, which passes net_slack, a sta::Delay
from resizeNetSlack, as the slack:{} argument, because Delay has
no fmt formatter (same
fmt::detail::type_is_unformattable_for<sta::Delay, char>
diagnostic as section 5). The port copy passes net_slack.mean()
instead of net_slack. Output is identical: Delay's own operator
float() returns mean(). No change to the format string or the
other arguments.
Correction to section 5: its sweep claim ("the only logger or
fmt call passing a sta::Delay") was wrong. It covered direct
log_ calls but missed this site because it travels through the
debugPrint macro (Logger.h expands debugPrint to
logger->debug), not a direct log call.
Sweep for this fix (whole place module, gpl, dpl, runner,
headers; direct log_ calls, logger calls, fmt::format, and
debugPrint macro sites; 159 sites inventoried): Delay, Slack,
and Arrival values (Arrival is a Delay alias) occur only in
timingBase.cpp, from resizeNetSlack, and both format arguments
are now .mean() (lines 165 and 191). The replace.cpp Slack hit
is the resizeSlackPreamble() method name, not a value; mbff.cpp
uses sta:: pointers only and its log arguments are ints, floats,
strings, and names; place.cc constructs the STA session only and
has no format call; no place header has a format call.
Corroboration: the owner's Mac build compiled every other place
translation unit with only the timingBase error, so no other
unformattable argument hides in the module.
