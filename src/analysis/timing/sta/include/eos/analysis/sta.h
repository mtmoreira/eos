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
// This header is a stable surface.
#ifndef EOS_ANALYSIS_STA_H_
#define EOS_ANALYSIS_STA_H_

#include <memory>
#include <string>

#include "eos/foundation/errors.h"

namespace eos::analysis {

using foundation::Result;
using foundation::Status;

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

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace eos::analysis

#endif  // EOS_ANALYSIS_STA_H_
