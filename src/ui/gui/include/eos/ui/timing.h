// timing: the GUI's timing model (L6 interface, Qt-free).
//
// The model the Timing panel reads: the structured timing
// summary from a timing_summary payload. Parsing is
// all-or-nothing like the other models: a malformed payload
// leaves the previous timing untouched and reports false.
// Measured values stay as the payload's own tokens, so the
// panel renders exactly what the command measured; counts are
// parsed integers. A payload value may be a brace group
// ({get_ports vclk}), which may contain spaces; the parser is
// brace-aware.
#ifndef EOS_UI_TIMING_H_
#define EOS_UI_TIMING_H_

#include <cstddef>
#include <string>
#include <vector>

namespace eos::ui {

// One scene's slack summary, from the last run_timing: the
// four slack metrics plus the worst electrical value and the
// violating-pin count per electrical check.
struct TimingScene {
  std::string scene;
  std::string mode;
  std::string corner;
  std::string parasitics;
  std::string worst_slack_ns;
  std::string total_negative_slack_ns;
  std::string worst_hold_slack_ns;
  std::string total_negative_hold_slack_ns;
  std::string max_transition_ns;
  std::size_t max_transition_violations = 0;
  std::string max_capacitance_pf;
  std::size_t max_capacitance_violations = 0;
  std::string max_fanout;
  std::size_t max_fanout_violations = 0;
};

// One clock-table row: the clock name, whether it is a
// generated clock, the period and waveform rise/fall in
// nanoseconds ("-" when underivable), the attribute letters,
// the skew ("none": no clock uncertainty is defined yet), and
// the sources in braces.
struct TimingClock {
  std::string name;
  bool generated = false;
  std::string period;
  std::string rise;
  std::string fall;
  std::string attrs;
  std::string skew;
  std::string sources;
};

// One generated-clock row: the generated name, its master
// clock ("-" when combinational), the master source, the
// waveform modification, and the generated sources.
struct TimingGeneratedClock {
  std::string name;
  std::string master_clock;
  std::string master_source;
  std::string modification;
  std::string sources;
};

// One clock construction record (bind_clock, no period): the
// name and the target in braces.
struct TimingBindClock {
  std::string name;
  std::string target;
};

// One worst-violator row: the scene, the constraint class
// (max_delay, min_delay, max_transition, max_capacitance,
// max_fanout), the worst violator ("-" when none violates),
// its slack ("none" when the class has no measured slack),
// the exact cost ("none" when unmeasured), the weight (1),
// the violating-pin count, and whether the class carries a
// limit at all (a DRC class with no limit reports limit=none).
struct TimingViolator {
  std::string scene;
  std::string klass;
  std::string violator;
  std::string slack;
  std::string cost;
  std::size_t violations = 0;
  bool has_limit = true;
};

// One worst path: the scene, the check (setup or hold), the
// endpoint and startpoint pins, and the slack, arrival, and
// required time in nanoseconds.
struct TimingPath {
  std::string scene;
  std::string check;
  std::string endpoint;
  std::string startpoint;
  std::string slack;
  std::string arrival;
  std::string required;
};

// One scene's coverage totals: the defined-check count and
// the tested/untested split.
struct TimingCoverageTotal {
  std::string scene;
  std::size_t checks = 0;
  std::size_t tested = 0;
  std::size_t untested = 0;
};

// One check type's coverage in one scene: the tested and
// untested counts and the tested percent.
struct TimingCoverageType {
  std::string scene;
  std::string type;
  std::size_t tested = 0;
  std::size_t untested = 0;
  std::string percent;
};

// One scene's QoR scalars: the setup and hold group counts,
// the union and every-group TNS with their endpoint counts,
// the cell census, and the compile CPU seconds ("n/a" when no
// synthesis run was recorded in this session).
struct TimingQor {
  std::string scene;
  std::size_t setup_groups = 0;
  std::size_t hold_groups = 0;
  std::string union_tns;
  std::size_t union_endpoints = 0;
  std::string every_group_tns;
  std::size_t every_group_endpoints = 0;
  std::string hold_union_tns;
  std::size_t hold_union_endpoints = 0;
  std::string hold_every_group_tns;
  std::size_t hold_every_group_endpoints = 0;
  std::size_t cells = 0;
  std::string area;
  std::string compile_cpu_s;
};

// One path group's QoR: the scene, the group name, the check,
// and the group's worst slack, TNS, and violating-path count.
struct TimingQorGroup {
  std::string scene;
  std::string group;
  std::string check;
  std::string wns;
  std::string tns;
  std::size_t violating_paths = 0;
};

// One cell type's QoR census row: the scene, the cell type,
// its count, and its summed Liberty area.
struct TimingQorCell {
  std::string scene;
  std::string cell;
  std::size_t count = 0;
  std::string area;
};

// One DRC class's QoR row: the scene, the class, the
// violating-pin count, and the worst slack ("no limit" when
// the class carries no limit).
struct TimingQorDrc {
  std::string scene;
  std::string drc;
  std::size_t violations = 0;
  std::string worst_slack;
};

class TimingModel {
 public:
  // Parses a timing_summary payload: a "source=<verilog|odb>
  // scenes=<n> revision=<n>" header line, then "clock",
  // "genclock", and "bindclock" rows, then per scene a "scene="
  // summary line, five "scene=... class=" violator rows (one
  // per class), "path" rows, "coverage" totals and per-type
  // rows, and "qor" scalar, group, cell, and DRC rows. A
  // scene-referencing row must name a scene the payload already
  // summarized, and the header's scene count must match the
  // summary rows. Returns false (timing unchanged) on any
  // malformed line, unknown key, unknown value, duplicate
  // scalar row, missing scene, missing class, or count
  // mismatch. A successful load replaces the previous timing.
  bool LoadPayload(const std::string& payload);
  void Clear();

  bool Loaded() const { return loaded_; }
  const std::string& Source() const { return source_; }
  const std::vector<TimingScene>& Scenes() const { return scenes_; }
  const std::vector<TimingClock>& Clocks() const { return clocks_; }
  const std::vector<TimingGeneratedClock>& GeneratedClocks() const {
    return generated_;
  }
  const std::vector<TimingBindClock>& BindClocks() const {
    return bind_clocks_;
  }
  const std::vector<TimingViolator>& Violators() const {
    return violators_;
  }
  const std::vector<TimingPath>& Paths() const { return paths_; }
  const std::vector<TimingCoverageTotal>& CoverageTotals() const {
    return coverage_totals_;
  }
  const std::vector<TimingCoverageType>& CoverageTypes() const {
    return coverage_types_;
  }
  const std::vector<TimingQor>& QorScenes() const { return qor_; }
  const std::vector<TimingQorGroup>& QorGroups() const {
    return qor_groups_;
  }
  const std::vector<TimingQorCell>& QorCells() const { return qor_cells_; }
  const std::vector<TimingQorDrc>& QorDrcs() const { return qor_drcs_; }

 private:
  bool loaded_ = false;
  std::string source_;
  std::vector<TimingScene> scenes_;
  std::vector<TimingClock> clocks_;
  std::vector<TimingGeneratedClock> generated_;
  std::vector<TimingBindClock> bind_clocks_;
  std::vector<TimingViolator> violators_;
  std::vector<TimingPath> paths_;
  std::vector<TimingCoverageTotal> coverage_totals_;
  std::vector<TimingCoverageType> coverage_types_;
  std::vector<TimingQor> qor_;
  std::vector<TimingQorGroup> qor_groups_;
  std::vector<TimingQorCell> qor_cells_;
  std::vector<TimingQorDrc> qor_drcs_;
};

}  // namespace eos::ui

#endif  // EOS_UI_TIMING_H_
