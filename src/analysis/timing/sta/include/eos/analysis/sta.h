// sta: the EOS facade over the imported OpenSTA analyzer (L3
// analysis).
//
// OpenSTA is adopted as EOS's static timing engine: its code lives
// in this module as EOS's own code (see PROVENANCE.md) and builds as
// the eda_analysis_sta target. This header is the only surface other
// modules may use. It exposes no upstream sta types: callers load a
// Liberty library, load a gate-level Verilog netlist, link the
// design, read SDC constraints (and optionally SPEF parasitics), and
// ask for the worst slack and total negative slack of the setup
// (max) analysis, in nanoseconds. The upstream Sta object, its Tcl
// interpreter and every upstream object stay behind the
// implementation in src/sta_facade.cc.
//
// Upstream Sta keeps a single global instance, so only one
// StaEngine may be alive at a time. Constructing a second engine
// while one is alive is reported by its operations failing with
// kInvalidState; engines used strictly one after another (create,
// use, destroy) work as expected.
//
// Beyond the scalar slacks, the facade reports the violating
// endpoints of each check kind (setup and hold) and, for one
// endpoint, its worst path as an ordered pin sequence, both
// derived from the upstream PathEnd and PathExpanded machinery
// behind the implementation. Endpoint identity is the pin's path
// name (instance/port, or the port name for a top-level port).
// The violation counts are the upstream analyzer's own counts,
// with its own float tolerance (1e-15 in the check's internal
// unit; for capacitance that is 1 fF, so a sub-fF slack is not
// counted as a violation by upstream, and is not counted here).
//
// The facade also reports the three electrical limit checks the
// upstream analyzer already computes (unit F5, the bounded part
// of limitation P2-4): max transition, max capacitance and max
// fanout. For each check it reports the worst checked pin (the
// pin with the smallest limit slack), that pin's measured value,
// its limit and its slack (limit minus value), and the number of
// pins that violate the check, all in the check's own unit
// (nanoseconds for transition, picofarads for capacitance, and
// the dimensionless fanout load for fanout). A check no pin
// carries a limit for reports has_limit false, a zero value and
// a zero violation count: no limit was checked, and the facade
// says so rather than implying a clean check ran against one.
//
// This header is a stable surface.
#ifndef EOS_ANALYSIS_STA_H_
#define EOS_ANALYSIS_STA_H_

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "eos/foundation/errors.h"

namespace eos::analysis {

using foundation::Result;
using foundation::Status;

// Which check an endpoint report or a worst path belongs to:
// setup checks are the max analysis, hold checks the min analysis.
enum class CheckKind : int {
  kSetup = 0,
  kHold = 1,
};

// One violating endpoint: the endpoint pin's path name and its
// worst slack for the check kind, in nanoseconds (negative by
// construction; only violating endpoints are reported).
struct EndpointSlack {
  std::string pin;
  double slack_ns = 0.0;
};

// One electrical limit check, worst pin first: the measured
// value, limit and slack of the pin with the smallest slack for
// the check, and how many pins violate it. worst_pin is empty
// and has_limit is false when no pin in the design carries a
// limit for this check. Units are the caller's documented unit
// for the check (see the class banner): the value, limit and
// slack of one check always share one unit.
struct ElectricalCheck {
  std::string worst_pin;
  double worst_value = 0.0;
  double worst_limit = 0.0;
  double worst_slack = 0.0;
  std::size_t violation_count = 0;
  bool has_limit = false;
};

// One analysis-coverage row (Immediate unit 7, review item I4):
// one check type evaluated at one constrained pin. The check type
// is one of setup, hold, out_setup, out_hold, recovery, removal,
// clock_gating_setup, clock_gating_hold, no-change,
// min_pulse_width. The related pin is the pin the check is
// measured against: the clock pin for setup, hold, recovery,
// removal and the clock-gating checks; the from pin for no-change;
// the output delay's reference pin, or its clock name when the
// delay has no reference pin, for out_setup and out_hold; "-"
// for min_pulse_width, which is measured on the pin itself, and
// for a set_max_delay/set_min_delay endpoint, whose constraining
// object is a from/to pair rather than one pin. tested is true
// exactly when the engine visited a constrained path end of this
// type at the pin (or, for min_pulse_width, a min-pulse-width
// check). untested_reason is empty when tested, and otherwise one
// of constant_disabled (the pin is constant or the check is
// disabled), no_clock (the check's clock is absent), 
// no_startpoint_clock (the clock is present but no clocked path
// reaches the pin), no_paths (the clock and a clocked path are
// present but no constrained path end was visited).
struct CoveragePin {
  std::string pin;
  std::string related_pin;
  std::string check_type;
  bool tested = false;
  std::string untested_reason;
};

// One violating pin of an electrical check: the pin's path name,
// the measured value, the limit it violates and the slack, all in
// the check's own unit (nanoseconds for max transition,
// picofarads for max capacitance, the dimensionless fanout load
// for max fanout). The slack is negative by construction: only
// pins that violate appear in a violator list.
struct ElectricalViolation {
  std::string pin;
  double value = 0.0;
  double limit = 0.0;
  double slack = 0.0;
};

// One point on a worst path, in path order: the pin's path name,
// the path name of the instance that owns the pin (empty for a
// top-level port pin, which no instance owns), the owning cell's
// name (empty for a port), the transition edge ("^" for rise, "v"
// for fall, empty when the analyzer reports none), the load-pin
// fanout (0 for a
// non-driver pin; in this version a plain count of load pins on
// the pin's net, without Liberty fanout-load weighting), the load
// capacitance in picofarads (-1 for a non-driver pin, where the
// analyzer reports no load), and the slew, incremental delay and
// cumulative arrival in nanoseconds. The incremental delay is the
// arrival difference from the previous point; for the first point
// it is the arrival itself.
struct PathPoint {
  std::string pin;
  std::string instance;
  std::string cell;
  std::string edge;
  double fanout = 0.0;
  double cap_pf = -1.0;
  double slew_ns = 0.0;
  double incr_ns = 0.0;
  double arrival_ns = 0.0;
};

// One worst path as a report: the endpoint pin, the startpoint pin
// (the first point's pin), the endpoint slack, the data arrival at
// the endpoint and the data required time (arrival plus slack),
// all in nanoseconds, plus the enriched points.
struct PathReport {
  std::string endpoint;
  std::string startpoint;
  double slack_ns = 0.0;
  double arrival_ns = 0.0;
  double required_ns = 0.0;
  std::vector<PathPoint> points;
};

// One path group: its name and every constrained endpoint in it
// (pin path name and worst slack in nanoseconds), worst first. An
// endpoint appears in every group it belongs to: group_path
// exceptions take precedence over the clock group, so one
// endpoint can sit in several named groups at once, and the union
// TNS counts it once while the every-group TNS counts it once per
// group. The default groups are named after their clocks; the
// special groups are named "path_delay", "gated_clk", "async"
// and "unconstrained" upstream.
struct PathGroupSlack {
  std::string name;
  std::vector<EndpointSlack> endpoints;
};

// One Liberty cell type's share of the design: the instance count
// and the summed Liberty area. Instances with no Liberty cell are
// counted under "(no liberty cell)" with zero area.
struct CellTypeArea {
  std::string cell;
  std::size_t count = 0;
  double area = 0.0;
};

// The design's cell census: one row per Liberty cell type present
// as a leaf instance, sorted by count descending then cell name,
// plus the totals. Area is the sum of the Liberty area
// attributes, in the library's own area unit.
struct CellAreaResult {
  std::vector<CellTypeArea> rows;
  std::size_t total_cells = 0;
  double total_area = 0.0;
};

// One timing analysis session. Move-only; the destructor releases
// the upstream analyzer and its interpreter. The intended order is
// ReadLiberty, ReadVerilog, LinkDesign, ReadSdc, then the slack
// queries; ReadSpef may be called after linking to annotate
// parasitics before querying. ClearDesign (synthesis unit S3)
// resets the session for the next candidate netlist without
// releasing the engine: the Liberty stays loaded, so a loop that
// evaluates many candidates pays the library load once.
class StaEngine {
 public:
  StaEngine();
  ~StaEngine();

  StaEngine(const StaEngine&) = delete;
  StaEngine& operator=(const StaEngine&) = delete;
  StaEngine(StaEngine&&) noexcept;
  StaEngine& operator=(StaEngine&&) noexcept;

  // Loads one Liberty library. Fails with kIoError when the file
  // cannot be read or does not parse.
  Status ReadLiberty(const std::string& path);
  // Loads one gate-level Verilog netlist. Fails with kIoError when
  // the file cannot be read or does not parse.
  Status ReadVerilog(const std::string& path);
  // Links the design with top_cell as the top module. Fails with
  // kIoError when linking fails (for example an unknown top cell).
  Status LinkDesign(const std::string& top_cell);
  // Reads one SDC constraint file. Fails with kIoError when the
  // file cannot be read or does not evaluate.
  Status ReadSdc(const std::string& path);
  // Reads one SPEF parasitics file. Fails with kIoError when the
  // file cannot be read or does not parse.
  Status ReadSpef(const std::string& path);
  // Clears the timing graph, the SDC and the parasitics, and
  // resets the linked and constrained flags, so the session can
  // evaluate another netlist: ReadVerilog, LinkDesign and
  // ReadSdc run again on the cleared session, and the slack
  // queries work exactly as on the first design. Module
  // definitions are replaced when the next netlist is read
  // (upstream deletes a same-named cell before redefining it),
  // and the Liberty libraries are NOT cleared: upstream keeps
  // its library scenes across a clear, which is what makes a
  // multi-candidate loop session-resident instead of a fresh
  // session per candidate. A cleared session answers no slack
  // query until it is linked and constrained again. Fails with
  // kInvalidState when the engine does not own the upstream
  // singleton.
  Status ClearDesign();

  // Worst setup (max) slack in nanoseconds. Fails with
  // kInvalidState before the design is linked and constrained.
  Result<double> WorstSlackNs();
  // Total negative setup (max) slack in nanoseconds: the sum of the
  // negative endpoint slacks, or zero when no endpoint violates.
  // Fails with kInvalidState under the same conditions as
  // WorstSlackNs.
  Result<double> TotalNegativeSlackNs();
  // Worst hold (min) slack in nanoseconds. Fails with kInvalidState
  // under the same conditions as WorstSlackNs.
  Result<double> WorstHoldSlackNs();
  // Total negative hold (min) slack in nanoseconds: the sum of the
  // negative hold endpoint slacks, or zero when no endpoint
  // violates. Fails with kInvalidState under the same conditions
  // as WorstSlackNs.
  Result<double> TotalNegativeHoldSlackNs();

  // The violating endpoints for one check kind: every endpoint
  // whose worst slack for that kind is negative, sorted by slack
  // ascending (worst first), ties broken by pin path name so the
  // order is deterministic. Empty when no endpoint violates.
  // Fails with kInvalidState under the same conditions as
  // WorstSlackNs.
  Result<std::vector<EndpointSlack>> ViolatingEndpoints(CheckKind kind);
  // The worst path to one endpoint for one check kind, as the
  // ordered sequence of points from the path's startpoint to the
  // endpoint itself (the last point's pin is the endpoint).
  // Fails with kInvalidState under the same conditions as
  // WorstSlackNs, and with kInvalidArgument when no endpoint pin
  // has the given path name, or when the named endpoint has no
  // constrained path end for that check kind.
  Result<std::vector<PathPoint>> WorstPath(CheckKind kind,
                                           const std::string& endpoint_pin);
  // Every constrained endpoint for one check kind, sorted by slack
  // ascending (worst first), ties broken by pin path name. Unlike
  // ViolatingEndpoints this includes endpoints that meet timing.
  // Fails with kInvalidState under the same conditions as
  // WorstSlackNs.
  Result<std::vector<EndpointSlack>> EndpointsBySlack(CheckKind kind);
  // The worst path to one endpoint as a report. Fails with
  // kInvalidState under the same conditions as WorstSlackNs, and
  // with kInvalidArgument when no endpoint pin has the given path
  // name, or when the named endpoint has no constrained path end
  // for that check kind.
  Result<PathReport> WorstPathReport(CheckKind kind,
                                     const std::string& endpoint_pin);
  // The path groups for one check kind: every constrained
  // endpoint bucketed by the group it belongs to, worst slack
  // first within each group, groups sorted by name so the order
  // is deterministic. Setup groups carry the max ends (setup,
  // recovery, gated-clock setup); hold groups the min ends
  // (hold, removal, gated-clock hold). Unconstrained endpoints
  // are never listed: an end with no clocked path is not in any
  // reported group. Fails with kInvalidState under the same
  // conditions as WorstSlackNs.
  Result<std::vector<PathGroupSlack>> PathGroupSlacks(CheckKind kind);
  // The design's cell census: one row per Liberty cell type
  // present as a leaf instance, plus the totals. Fails with
  // kInvalidState under the same conditions as WorstSlackNs.
  Result<CellAreaResult> CellArea();

  // The max transition (slew) check: worst value, limit and
  // slack in nanoseconds, and the violating-pin count. Fails
  // with kInvalidState under the same conditions as
  // WorstSlackNs.
  Result<ElectricalCheck> MaxTransitionCheck();
  // The max capacitance check: worst value, limit and slack in
  // picofarads, and the violating-pin count. Fails with
  // kInvalidState under the same conditions as WorstSlackNs.
  Result<ElectricalCheck> MaxCapacitanceCheck();
  // The max fanout check: worst value, limit and slack in the
  // dimensionless fanout load, and the violating-pin count.
  // Fails with kInvalidState under the same conditions as
  // WorstSlackNs.
  Result<ElectricalCheck> MaxFanoutCheck();
  // Every pin violating the max transition check, sorted by slack
  // ascending (worst first), ties broken by pin path name so the
  // order is deterministic. The list is empty when no pin
  // violates, including when no pin carries a limit. Its size is
  // the check's violation count. Fails with kInvalidState under
  // the same conditions as WorstSlackNs.
  Result<std::vector<ElectricalViolation>> MaxTransitionViolators();
  // Every pin violating the max capacitance check, with the same
  // ordering and failure behavior as MaxTransitionViolators.
  Result<std::vector<ElectricalViolation>> MaxCapacitanceViolators();
  // Every pin violating the max fanout check, with the same
  // ordering and failure behavior as MaxTransitionViolators. A pin
  // violating in more than one analysis mode appears once per
  // violating mode, matching the upstream bulk check's count.
  Result<std::vector<ElectricalViolation>> MaxFanoutViolators();
  // The analysis coverage of the loaded design (Immediate unit 7,
  // review item I4): one CoveragePin per (pin, check type) where
  // the check is defined, sorted by pin path name then check
  // type so the order is deterministic. A check type with no
  // defined check anywhere in the design contributes no rows.
  // Fails with kInvalidState under the same conditions as
  // WorstSlackNs.
  Result<std::vector<CoveragePin>> AnalysisCoverage();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace eos::analysis

#endif  // EOS_ANALYSIS_STA_H_
