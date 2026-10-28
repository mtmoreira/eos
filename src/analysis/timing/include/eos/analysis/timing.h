// timing: the timing service over scenes (L3 analysis).
//
// One service fronts every timing query in EOS: given a design and
// a set of scenes (a scenario, the Liberty for its corner, and the
// parasitics its fidelity level calls for), it runs the STA engine
// once per scene and returns one metric map per scene. Scenes are
// the unit of MCMM honesty (Revised Plan, Sections 7 and 8): a
// scene's parasitics level is never mixed silently, so an extracted
// scene REQUIRES the SPEF that backs it and an estimate scene must
// not carry one; either mismatch is an error before any analysis
// runs, not a quiet fallback.
//
// The design enters in one of two forms: a gate-level Verilog
// netlist (with its SDC), analyzed through the STA facade directly;
// or an imported ODB database, whose top block the ODB facade
// writes out as a netlist (see eos/db/odb.h for the physical-only
// cell rule) which is then analyzed the same way, with the same
// SDC. The metrics are setup worst slack and total negative slack
// plus the hold pair, all in nanoseconds, named exactly as the
// session CostPolicy expects (worst_slack_ns first among them).
// Each scene also carries the three electrical limit checks of
// unit F5 (the bounded part of limitation P2-4), from the STA
// facade's ElectricalCheck results: for max transition, max
// capacitance and max fanout, the worst checked value in the
// check's own unit (max_transition_ns in ns, max_capacitance_pf
// in pF, max_fanout in the dimensionless fanout load) and the
// violating-pin count (max_transition_violations,
// max_capacitance_violations and max_fanout_violations, unit
// "pins"). A check no pin carries a limit for reports a worst
// value of 0 and a count of 0, exactly as the facade reports it.
//
// Each scene's result also carries the scene's violating
// endpoints for the setup and hold checks (pin path name and
// worst slack in nanoseconds, worst first), surfaced from the STA
// facade's endpoint reporting, so closure consumers can aim at
// the endpoints that actually violate instead of enumerating the
// whole design. The facade's worst-path query stays available
// per engine session for callers that need a critical cone.
//
// Report registration is deliberately NOT here: bindings live in
// L5 session state, which an L3 service may not reach up to. The
// service returns the per-scene results; the caller (the L6
// command layer, later the L5 flows) registers one report binding
// per scene through session/reports at its current ledger
// revision.
//
// This header is a stable surface.
#ifndef EOS_ANALYSIS_TIMING_H_
#define EOS_ANALYSIS_TIMING_H_

#include <map>
#include <string>
#include <vector>

#include "eos/analysis/sta.h"
#include "eos/db/odb.h"
#include "eos/db/scenarios.h"
#include "eos/foundation/errors.h"

namespace eos::analysis {

using foundation::Result;
using foundation::Status;

// One measured value with its unit, mirroring the session ledger's
// MetricValue shape (the ledger is L5 and cannot be included from
// here; callers convert field by field).
struct TimingMetric {
  double value = 0.0;
  std::string unit;
};

using TimingMetricMap = std::map<std::string, TimingMetric>;

// Everything one scene's analysis needs beyond the design itself.
struct SceneAnalysis {
  // The scenario this scene analyzes under. Its (mode, corner,
  // parasitics) triple is the scene's identity.
  db::Scenario scenario;
  // The Liberty library for the scenario's corner.
  std::string liberty_path;
  // The SPEF parasitics file. Required exactly when the scenario's
  // parasitics level is kExtracted; must be empty for kEstimate.
  std::string spef_path;
};

// One scene's outcome: the scenario it ran under and its metrics
// (worst_slack_ns, total_negative_slack_ns, worst_hold_slack_ns,
// total_negative_hold_slack_ns; all unit "ns"; plus the six
// electrical metrics named in the banner above), plus the
// violating endpoints of each check kind for that scene, in the
// STA facade's EndpointSlack shape, worst first. An empty list
// means no endpoint violates that check in this scene.
struct SceneResult {
  db::Scenario scenario;
  TimingMetricMap metrics;
  std::vector<EndpointSlack> setup_violating_endpoints;
  std::vector<EndpointSlack> hold_violating_endpoints;
};

// A design in Verilog form: the netlist, its top cell, and the SDC
// constraint file every scene is analyzed under.
struct VerilogDesign {
  std::string verilog_path;
  std::string top_cell;
  std::string sdc_path;
};

class TimingService {
 public:
  // Analyzes the Verilog design under every scene, in the order
  // given. Fails with kInvalidArgument on an empty scene list, a
  // duplicate scenario (by id, name or triple), an empty Liberty
  // or SDC path, or a parasitics/SPEF mismatch in any scene; the
  // checks all run before the first analysis, so a bad list fails
  // whole. A failure inside one scene's analysis (an unreadable
  // Liberty, an unlinkable netlist) fails the call with that
  // status and names the scene.
  Result<std::vector<SceneResult>> Analyze(
      const VerilogDesign& design,
      const std::vector<SceneAnalysis>& scenes);

  // Analyzes an imported ODB database under every scene. The
  // database's top block is written to a temporary netlist through
  // the ODB facade and analyzed exactly as the Verilog form, under
  // the given SDC. Fails with kInvalidState when the database has
  // no top block, plus the Analyze failures above.
  Result<std::vector<SceneResult>> Analyze(
      db::OdbDatabase& database, const std::string& sdc_path,
      const std::vector<SceneAnalysis>& scenes);
};

}  // namespace eos::analysis

#endif  // EOS_ANALYSIS_TIMING_H_
