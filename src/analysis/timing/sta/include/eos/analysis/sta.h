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
//
// This header is a stable surface.
#ifndef EOS_ANALYSIS_STA_H_
#define EOS_ANALYSIS_STA_H_

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

// One point on a worst path, in path order: the pin's path name
// and the path name of the instance that owns the pin (empty for
// a top-level port pin, which no instance owns).
struct PathPoint {
  std::string pin;
  std::string instance;
};

// One timing analysis session. Move-only; the destructor releases
// the upstream analyzer and its interpreter. The intended order is
// ReadLiberty, ReadVerilog, LinkDesign, ReadSdc, then the slack
// queries; ReadSpef may be called after linking to annotate
// parasitics before querying.
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

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace eos::analysis

#endif  // EOS_ANALYSIS_STA_H_
