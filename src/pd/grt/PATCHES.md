# grt: port adaptations (EOS-owned changes to the imported copies)

The pin tree is never modified. Every change below lives only in
the EOS port copies in this module, and is recorded here so the
difference from upstream ead3236 is auditable. Nothing else in
this module differs from upstream except the EOS-owned files
(src/MakeGlobalRouter.cc, internal/sta/Set.hh, CMakeLists.txt,
README.md, PROVENANCE.md, this file).

## 1. GlobalRouter.cpp: wire-parasitics builder deferred (P2 scope)

Upstream GlobalRouter.cpp constructs a MakeWireParasitics builder
in findRouting, estimateRC(), estimateRC(dbNet*), and
routeLayerLengths, and calls sta_->setParasiticAnalysisPts(true,
false) in estimateRC(). MakeWireParasitics cannot compile against
the in-tree STA: it uses sta_->corners(),
corner->findParasiticAnalysisPt(min_max),
sta_->setParasiticAnalysisPts(...), and the pin-era Corner and
ParasiticAnalysisPt classes, none of which exist in-tree (scenes
replace corners). A full port would be a Corner-to-Scene port
following the rsz EstimateWireParasitics.cc pattern, changing
MakeWireParasitics.h signatures from Corner*/ParasiticAnalysisPt*
to Scene*: P6 extraction-scale work, not among the authorized P2
port copies, and it would land untested because the placement
path never calls it. It is therefore DEFERRED to P4/P6, not
silently stubbed, and this module does not import
MakeWireParasitics.h/.cpp at all.

Ported GlobalRouter.cpp therefore:

- deletes the `#include "MakeWireParasitics.h"` line and the
  `sta_->setParasiticAnalysisPts(true, false)` call (the in-tree
  dbSta has no such method; the estimateRC bodies that used it are
  replaced wholesale, see below). Upstream GlobalRouter.cpp
  relied on MakeWireParasitics.h's transitive `#include "Net.h"`
  (it never includes Net.h itself), so the port adds a direct
  `#include "Net.h"` to GlobalRouter.cpp; without it the TU fails
  with incomplete-type errors on grt::Net;
- replaces `getNetSlack`'s body with
  `sta_->netSlack(net->getDbNet(), sta::MinMax::max())`, which is
  semantically identical to upstream's dbToSta + netSlack(sta_net)
  because the in-tree dbSta::netSlack(const dbNet*, MinMax*)
  internally does dbToSta + slack();
- in findRouting, passes a null builder to FastRoute and fails
  loudly (Logger::error GRT 400, noreturn) when
  fastroute_->getCriticalNetsPercentage() > 0, instead of
  constructing the builder. This is safe for placement:
  FastRouteCore::CalculatePartialSlack() is the only place that
  dereferences the builder, and it runs only when the critical
  nets percentage is nonzero; the placement routability path
  (RouteBase) explicitly sets it to 0 ("this option must be off"),
  and GlobalRouter::initFastRoute / setCriticalNetsPercentage
  force it to 0 when no default Liberty library exists;
- replaces estimateRC(), estimateRC(dbNet*), and
  routeLayerLengths(dbNet*) wholesale with bodies that call
  Logger::error (GRT 401, 402, 403 respectively, noreturn) stating
  that the method needs the wire-parasitics builder, which is not
  ported in P2. These are loud failures, not silent no-ops: a
  caller gets an error, never a wrong result. Note estimateRC()
  no longer clears existing parasitics (upstream called
  sta_->deleteParasitics() before building); the whole method is
  deferred, so its old side effect is gone with it.

Follow-up owed: P4 global route with critical nets above 0, and
P6 estimateRC, must restore the ported builder first.

## 2. FastRoute DataType.h: fmt 9.1 scoped-enum formatter

System fmt 9.1 has no formatter for scoped enums, and
FastRoute's utility.cpp formats `edge.route.type` (a RouteType
scoped enum) through Logger::report. Upstream pins fmt 8.1.1 on
macOS for the same reason (P0 section 5c). The port copy of
include/fastroute/DataType.h adds, after the existing
`operator<<(std::ostream&, RouteType)` declaration, a guarded
`fmt::formatter<grt::RouteType> : fmt::ostream_formatter`
specialization that delegates to that operator, under
`#if __has_include(<fmt/ostream.h>)`. The specialization lives
in namespace fmt (with namespace grt closed and reopened around
it): a specialization inside namespace grt is rejected by GCC,
and the `<fmt/ostream.h>` include must also sit outside grt so
its contents land in fmt rather than grt::fmt. The enum
definition itself is unchanged. This is EOS-owned glue in the
port copy (the pin tree is untouched); the brief's "resolve in
EOS-owned glue, stop if it requires patching an upstream file"
is satisfied in that sense, and the driver must judge this
interpretation: the change is in an imported file's copy, not in
a separate file, because the formatter must be visible to
utility.cpp's translation unit through the DataType.h it already
includes.

## 3. sta/Set.hh shim (internal/sta/Set.hh, EOS-owned)

GlobalRouter.cpp includes "sta/Set.hh" but never uses sta::Set.
The in-tree STA has no Set class, and the pin-era OpenSTA header
of that name is GPL-3.0, so it is deliberately not imported.
This module provides its own fresh shim at internal/sta/Set.hh
(added as a PRIVATE include dir, so "sta/Set.hh" resolves
there), following the P0 section 5b shim approach: pin-era API
surface, marked as a shim, not an upstream file and not a
modification of the sta module. The shim is written fresh in EOS
style, not copied from the upstream header.

## 4. FastRoute include directory (CMakeLists.txt, EOS-owned glue)

Upstream fastroute translation units include their headers bare
(AbstractFastRouteRenderer.h, DataType.h, FastRoute.h,
FastRouteRenderer.h), resolved upstream by the
src/grt/src/fastroute/include directory. The port moved those
headers to include/fastroute, so the grt CMakeLists adds
include/fastroute as a PRIVATE include directory (the same
pattern stt uses for include/stt). No imported source file is
edited to add a prefix, and the public header path
(fastroute/*.h) is unchanged for consumers.

## 5. EOS-owned factory (src/MakeGlobalRouter.cc)

Upstream's MakeGlobalRouter is Tcl/SWIG glue and is not imported.
This module provides an EOS-owned factory (include/grt/
MakeGlobalRouter.h) that constructs a GlobalRouter with the
routing-congestion heat-map data source its init requires
(heatMap.cpp, internal to this module). Callers never reach into
this module's src/ directory for it.
